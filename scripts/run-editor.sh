#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

echo "=== Launching KiteSurf in UnrealEditor (Vulkan) ==="
export __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia
# Offscreen runs are scripted and end on their own, so they queue for the GPU lock (common.sh).
# A window, game or editor, is someone at the screen and may stay open for hours; it does not
# take the lock, so it would hold up every queued run.
if has_arg -RenderOffScreen "$@"; then
    with_gpu_lock "$UE_EDITOR" "$UPROJECT" -vulkan "$@"
else
    exec "$UE_EDITOR" "$UPROJECT" -vulkan "$@"
fi
