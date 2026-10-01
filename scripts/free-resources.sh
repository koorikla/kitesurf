#!/usr/bin/env bash
set -euo pipefail

ACTION="${1:-}"

if [[ "$ACTION" != "stop" && "$ACTION" != "start" ]]; then
    echo "Usage: $0 stop|start" >&2
    exit 1
fi

echo "=== $ACTION k3s system service ==="
sudo systemctl "$ACTION" k3s
