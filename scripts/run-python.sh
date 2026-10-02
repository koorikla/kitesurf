#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

SCRIPT_ARG="${1:-$PROJECT_ROOT/scripts/editor/make_input_assets.py}"
SCRIPT_PATH="$(realpath "$SCRIPT_ARG")"

echo "=== Running Editor Python Script: $SCRIPT_PATH ==="
"$UE_EDITOR_CMD" "$UPROJECT" -run=pythonscript -Script="$SCRIPT_PATH" -unattended -nosplash -stdout "${@:2}"
