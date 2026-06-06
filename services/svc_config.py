#!/usr/bin/env python3
# Reads services.yaml and emits, for each service, a TAB-separated line:
#   name <TAB> port <TAB> launch-command
# The launcher (serviced.sh) and the orchestrator both consume this so the YAML
# is parsed in exactly one place.
import sys
import shlex
import yaml

CFG = "services/services.yaml"


def main():
    with open(CFG) as f:
        cfg = yaml.safe_load(f)

    if len(sys.argv) > 1 and sys.argv[1] == "--http":
        h = cfg.get("http", {})
        cb = h.get("circuit", {})
        print(f"retries\t{h.get('retries', 4)}")
        print(f"backoff_ms\t{h.get('backoff_ms', 200)}")
        print(f"failure_threshold\t{cb.get('failure_threshold', 3)}")
        print(f"cooldown_ms\t{cb.get('cooldown_ms', 5000)}")
        return

    if len(sys.argv) > 2 and sys.argv[1] == "--port":
        print(cfg["services"][sys.argv[2]]["port"])
        return

    for name, info in cfg["services"].items():
        port = info["port"]
        if "native" in info:
            cmd = info["native"]
        else:
            cmd = f"build/sidecar {port} {shlex.quote(info['sidecar'])}"
        print(f"{name}\t{port}\t{cmd}")


if __name__ == "__main__":
    main()
