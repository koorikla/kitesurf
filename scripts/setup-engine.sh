#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <path-to-engine-zip> [dest-dir=/opt/unreal-engine]" >&2
    exit 1
fi

ZIP_PATH="$(realpath "$1")"
DEST_DIR="${2:-/opt/unreal-engine}"

if [[ ! -f "$ZIP_PATH" ]]; then
    echo "ERROR: Engine zip file not found: $ZIP_PATH" >&2
    exit 1
fi

echo "=== Installing Unreal Engine 5.8 runtime dependencies via pacman ==="
sudo pacman -S --needed --noconfirm     vulkan-icd-loader     vulkan-tools     lib32-vulkan-icd-loader     sdl3     libxcursor     libxrandr     libxi     libxinerama     libxss     alsa-lib     dotnet-sdk     xdg-user-dirs

echo "=== Extracting engine archive to $DEST_DIR ==="
sudo mkdir -p "$DEST_DIR"
TMP_EXTRACT="$(sudo mktemp -d -p /opt .tmp_extract_XXXXXX)"
trap 'sudo rm -rf "$TMP_EXTRACT"' EXIT

sudo unzip -q "$ZIP_PATH" -d "$TMP_EXTRACT"

# Check if there is a single top-level directory and flatten if so
TOP_LEVEL_COUNT="$(sudo find "$TMP_EXTRACT" -mindepth 1 -maxdepth 1 | wc -l)"
if [[ "$TOP_LEVEL_COUNT" -eq 1 && -d "$(sudo find "$TMP_EXTRACT" -mindepth 1 -maxdepth 1)" ]]; then
    SINGLE_DIR="$(sudo find "$TMP_EXTRACT" -mindepth 1 -maxdepth 1)"
    echo "Flattening top-level directory: $SINGLE_DIR"
    sudo cp -a "$SINGLE_DIR/." "$DEST_DIR/"
else
    sudo cp -a "$TMP_EXTRACT/." "$DEST_DIR/"
fi

echo "=== Adjusting permissions on Engine directory ==="
sudo chmod -R a+rwX "$DEST_DIR/Engine"

# Write destination to .engine-path in repo if run from repo or project root
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
echo "$DEST_DIR" > "$PROJECT_ROOT/.engine-path"
echo "Saved engine path to $PROJECT_ROOT/.engine-path"

echo "=== GPU Summary ==="
vulkaninfo --summary 2>/dev/null | grep -E "GPU[0-9]|deviceName|driverVersion" || true
