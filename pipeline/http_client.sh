# ============================================================================
# http_client.sh — sourced by calc in --service mode. Provides http_post with
# exponential-backoff retries and a per-service circuit breaker, because calling
# a localhost function the hard way deserves production-grade resilience.
# ============================================================================

HTTP_RETRIES="${HTTP_RETRIES:-4}"
HTTP_BACKOFF_MS="${HTTP_BACKOFF_MS:-200}"
CB_THRESHOLD="${CB_THRESHOLD:-3}"
CB_COOLDOWN_MS="${CB_COOLDOWN_MS:-5000}"
CB_DIR="${CB_DIR:-${TMPDIR:-/tmp}/calc_circuit}"
mkdir -p "$CB_DIR"

_now_ms() { echo $(( $(date +%s%N) / 1000000 )); }
_sleep_ms() { sleep "$(awk "BEGIN{printf \"%.3f\", $1/1000}")"; }

# Circuit breaker: returns 0 (true) when the circuit for $1 is OPEN (blocked).
_cb_open() {
    local f="$CB_DIR/$1.open"
    [[ -f "$f" ]] || return 1
    local opened now
    opened="$(cat "$f")"; now="$(_now_ms)"
    if (( now - opened < CB_COOLDOWN_MS )); then
        return 0
    fi
    rm -f "$f" "$CB_DIR/$1.fail"   # cooldown elapsed -> half-open, allow a try
    return 1
}
_cb_fail() {
    local n="$1" f="$CB_DIR/$1.fail" c=0
    [[ -f "$f" ]] && c="$(cat "$f")"
    c=$((c + 1)); echo "$c" > "$f"
    if (( c >= CB_THRESHOLD )); then _now_ms > "$CB_DIR/$n.open"; fi
}
_cb_reset() { rm -f "$CB_DIR/$1.fail" "$CB_DIR/$1.open"; }

# http_post <name> <port> : reads body from stdin, writes response to stdout.
# Returns 0 on success, 7 if the circuit is open, 1 if all retries failed.
http_post() {
    local name="$1" port="$2" body out rc attempt=0 delay="$HTTP_BACKOFF_MS"
    body="$(cat)"

    if _cb_open "$name"; then
        echo "http_client: circuit OPEN for $name; failing fast" >&2
        return 7
    fi

    while (( attempt < HTTP_RETRIES )); do
        out="$(printf '%s' "$body" | curl -sS -f -m 20 --data-binary @- \
               "http://127.0.0.1:$port/" 2>/dev/null)"
        rc=$?
        if (( rc == 0 )); then
            _cb_reset "$name"
            printf '%s' "$out"
            return 0
        fi
        attempt=$((attempt + 1))
        if (( attempt < HTTP_RETRIES )); then
            echo "http_client: $name attempt $attempt failed (rc=$rc); retrying in ${delay}ms" >&2
            _sleep_ms "$delay"
            delay=$((delay * 2))
        fi
    done

    _cb_fail "$name"
    echo "http_client: $name exhausted $HTTP_RETRIES attempts" >&2
    return 1
}
