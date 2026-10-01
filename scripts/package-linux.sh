#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

BUILD_DIR="$PROJECT_ROOT/Build"
mkdir -p "$BUILD_DIR"

echo "=== Packaging KiteSurf for Linux Shipping ==="
"$UE_RUNUAT" BuildCookRun     -project="$UPROJECT"     -platform=Linux     -clientconfig=Shipping     -build     -cook     -stage     -pak     -archive     -archivedirectory="$BUILD_DIR"     "$@"
