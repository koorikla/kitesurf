#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

MAP="${1:-}"
FRAMES="${2:-600}"

echo "=== Running KiteSurf GPU Smoke Test ==="
if [[ -n "$MAP" ]]; then
    echo "Map: $MAP"
fi
echo "Frames: $FRAMES"

# Clean prior screenshots
mkdir -p "$PROJECT_ROOT/Saved/Screenshots/LinuxEditor"
rm -f "$PROJECT_ROOT/Saved/Screenshots/LinuxEditor"/*.png

# Run editor in game mode with Vulkan, off-screen rendering, and exit cleanly after N frames.
# kitesurf.SmokeFrames saves Saved/Screenshots/LinuxEditor/smoke.png shortly before exiting.
# Queues behind any other GPU run on the machine (see with_gpu_lock in common.sh).
with_gpu_lock "$UE_EDITOR" "$UPROJECT" ${MAP:+"$MAP"} \
    -vulkan \
    -game \
    -RenderOffScreen \
    -ResX=1280 \
    -ResY=720 \
    -log \
    -ExecCmds="kitesurf.SmokeFrames $FRAMES" \
    -unattended \
    "${@:3}"

SMOKE_EXIT=$?
if [[ $SMOKE_EXIT -ne 0 ]]; then
    echo "ERROR: GPU smoke test process failed with exit code $SMOKE_EXIT" >&2
    exit $SMOKE_EXIT
fi

# Check log for errors
LOG_FILE="$PROJECT_ROOT/Saved/Logs/KiteSurf.log"
python3 "$SCRIPT_DIR/ci/scan_game_log.py" "$LOG_FILE"

SCREENSHOT="$PROJECT_ROOT/Saved/Screenshots/LinuxEditor/smoke.png"
if [[ ! -s "$SCREENSHOT" ]]; then
    echo "ERROR: GPU smoke test did not produce $SCREENSHOT" >&2
    exit 1
fi

echo "GPU smoke test passed successfully ($FRAMES frames rendered, clean exit, 0 errors)!"
