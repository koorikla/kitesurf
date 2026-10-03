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

# The Mac editor writes its log only to ~/Library/Logs unless asked for stdout.
STDOUT=()
if [[ "$UE_HOST_PLATFORM" == Mac ]]; then
    STDOUT=(-stdout -FullStdOutLogOutput)
fi

echo "=== Running KiteSurf Automation Tests ==="
# A failed test makes the editor exit non-zero (on Mac); summarise the report first, then pass it on.
STATUS=0
${LOCK[@]+"${LOCK[@]}"} "$UE_EDITOR_CMD" "$UPROJECT"     -unattended     -nopause     -nosplash     -log     -ExecCmds="Automation RunTests KiteSurf; Quit"     -ReportExportPath="$REPORT_PATH"     ${STDOUT[@]+"${STDOUT[@]}"} "$@" || STATUS=$?

INDEX_JSON="$REPORT_PATH/index.json"
if [[ -f "$INDEX_JSON" ]]; then
    python3 "$SCRIPT_DIR/parse_test_report.py" "$INDEX_JSON"
else
    echo "WARNING: Test report index.json not found at $INDEX_JSON"
fi
exit "$STATUS"
