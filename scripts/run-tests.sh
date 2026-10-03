#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

REPORT_PATH="$PROJECT_ROOT/Saved/Automation/Report"
mkdir -p "$REPORT_PATH"

# Without -nullrhi the tests render on the GPU, so they queue for the GPU lock (common.sh).
LOCK=()
if ! has_arg -nullrhi "$@"; then
    LOCK=(with_gpu_lock)
fi

echo "=== Running KiteSurf Automation Tests ==="
"${LOCK[@]}" "$UE_EDITOR_CMD" "$UPROJECT"     -unattended     -nopause     -nosplash     -log     -ExecCmds="Automation RunTests KiteSurf; Quit"     -ReportExportPath="$REPORT_PATH"     "$@"

INDEX_JSON="$REPORT_PATH/index.json"
if [[ -f "$INDEX_JSON" ]]; then
    python3 "$SCRIPT_DIR/parse_test_report.py" "$INDEX_JSON"
else
    echo "WARNING: Test report index.json not found at $INDEX_JSON"
fi
