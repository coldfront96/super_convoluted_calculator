#!/usr/bin/env bash
# ============================================================================
# serviced.sh — launches/stops the (gloriously unnecessary) microservices mesh.
# Usage: services/serviced.sh {start|stop|status|restart}
# ============================================================================
set -u

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT" || exit 1
RUN="$ROOT/.services"
mkdir -p "$RUN"

config() { python3 services/svc_config.py; }

wait_health() {
    local port=$1 tries=0
    while (( tries < 100 )); do
        if curl -sf -m 2 "http://127.0.0.1:$port/health" >/dev/null 2>&1; then return 0; fi
        sleep 0.1
        tries=$((tries + 1))
    done
    return 1
}

start() {
    echo "starting microservices mesh..."
    while IFS=$'\t' read -r name port cmd; do
        if curl -sf -m 1 "http://127.0.0.1:$port/health" >/dev/null 2>&1; then
            echo "  $name already up on :$port"
            continue
        fi
        nohup bash -c "exec $cmd" >"$RUN/$name.log" 2>&1 &
        echo $! > "$RUN/$name.pid"
    done < <(config)

    local ok=1
    while IFS=$'\t' read -r name port cmd; do
        if wait_health "$port"; then
            printf "  \033[32m✓\033[0m %-12s :%s\n" "$name" "$port"
        else
            printf "  \033[31m✗\033[0m %-12s :%s  (see %s)\n" "$name" "$port" "$RUN/$name.log"
            ok=0
        fi
    done < <(config)
    [[ $ok -eq 1 ]] || { echo "some services failed to start"; return 1; }
    echo "mesh is up."
}

stop() {
    echo "stopping microservices mesh..."
    for pidf in "$RUN"/*.pid; do
        [[ -e "$pidf" ]] || continue
        local pid; pid="$(cat "$pidf")"
        # kill the launcher and any child it spawned
        pkill -P "$pid" 2>/dev/null
        kill "$pid" 2>/dev/null
        rm -f "$pidf"
    done
    echo "mesh is down."
}

status() {
    while IFS=$'\t' read -r name port cmd; do
        if curl -sf -m 1 "http://127.0.0.1:$port/health" >/dev/null 2>&1; then
            printf "  \033[32m●\033[0m %-12s :%s up\n" "$name" "$port"
        else
            printf "  \033[31m○\033[0m %-12s :%s down\n" "$name" "$port"
        fi
    done < <(config)
}

case "${1:-status}" in
    start)   start ;;
    stop)    stop ;;
    restart) stop; start ;;
    status)  status ;;
    *) echo "usage: $0 {start|stop|status|restart}" >&2; exit 64 ;;
esac
