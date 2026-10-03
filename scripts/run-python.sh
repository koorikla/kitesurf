#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

SCRIPT_ARG="${1:-$PROJECT_ROOT/scripts/editor/make_input_assets.py}"
SCRIPT_PATH="$(realpath "$SCRIPT_ARG")"

# Commandlets have no renderer unless asked for one; a rendering run queues for the GPU lock (common.sh).
LOCK=()
if has_arg -RenderOffScreen "${@:2}" || has_arg -AllowCommandletRendering "${@:2}"; then
    LOCK=(with_gpu_lock)
fi

echo "=== Running Editor Python Script: $SCRIPT_PATH ==="
"${LOCK[@]}" "$UE_EDITOR_CMD" "$UPROJECT" -run=pythonscript -Script="$SCRIPT_PATH" -unattended -nosplash -stdout "${@:2}"
