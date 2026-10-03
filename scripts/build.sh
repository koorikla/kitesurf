#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

CONFIG="${1:-Development}"

echo "=== Building KiteSurfEditor $UE_HOST_PLATFORM $CONFIG ==="
"$UE_BUILD" KiteSurfEditor "$UE_HOST_PLATFORM" "$CONFIG" -Project="$UPROJECT" -WaitMutex -FromMsBuild "${@:2}"
