#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_URL="${REPO_URL:-https://github.com/koorikla/kitesurf}"
REPO_SLUG="${REPO_SLUG:-koorikla/kitesurf}"
RUNNER_NAME="${RUNNER_NAME:-koorikla}"
RUNNER_DIR="${RUNNER_DIR:-$HOME/.local/share/actions-runner-kitesurf}"
SYSTEMD_DIR="$HOME/.config/systemd/user"
SERVICE_NAME="actions-runner-kitesurf.service"

command -v gh >/dev/null 2>&1 || { echo "ERROR: gh CLI required but not found" >&2; exit 1; }
command -v curl >/dev/null 2>&1 || { echo "ERROR: curl required but not found" >&2; exit 1; }
command -v tar >/dev/null 2>&1 || { echo "ERROR: tar required but not found" >&2; exit 1; }
command -v sha256sum >/dev/null 2>&1 || { echo "ERROR: sha256sum required but not found" >&2; exit 1; }

echo "=== Preparing Actions Runner installation at $RUNNER_DIR ==="
mkdir -p "$RUNNER_DIR" "$SYSTEMD_DIR"

# Determine latest runner release
RELEASE_DATA=$(gh api repos/actions/runner/releases/latest)
TAG_NAME=$(echo "$RELEASE_DATA" | jq -r '.tag_name')
VERSION="${TAG_NAME#v}"
ARCH="x64"
ARCHIVE="actions-runner-linux-${ARCH}-${VERSION}.tar.gz"
DOWNLOAD_URL="https://github.com/actions/runner/releases/download/${TAG_NAME}/${ARCHIVE}"

TMP_DIR=$(mktemp -d)""
trap 'rm -rf "$TMP_DIR"' EXIT

echo "Downloading GitHub Actions Runner ${VERSION}..."
curl -fsSL -o "$TMP_DIR/$ARCHIVE" "$DOWNLOAD_URL"

# Extract SHA256 checksum from release notes if present
EXPECTED_SHA=$(echo "$RELEASE_DATA" | jq -r '.body' | grep -A 1 "${ARCHIVE}" | grep -oE '[a-f0-9]{64}' | head -n 1 || true)
if [[ -n "$EXPECTED_SHA" ]]; then
    echo "Verifying sha256 ($EXPECTED_SHA)..."
    echo "$EXPECTED_SHA  $TMP_DIR/$ARCHIVE" | sha256sum -c -
else
    echo "Notice: Checksum not found in release body, skipping hash verification"
fi

# If runner already running, stop service first
if systemctl --user is-active --quiet "$SERVICE_NAME" 2>/dev/null; then
    echo "Stopping existing $SERVICE_NAME..."
    systemctl --user stop "$SERVICE_NAME" || true
fi

echo "Extracting runner into $RUNNER_DIR..."
tar -xzf "$TMP_DIR/$ARCHIVE" -C "$RUNNER_DIR"

echo "Requesting registration token from GitHub API..."
REG_TOKEN=$(gh api -X POST "repos/${REPO_SLUG}/actions/runners/registration-token" --jq .token)

echo "Configuring runner..."
(cd "$RUNNER_DIR" && ./config.sh \
    --url "$REPO_URL" \
    --token "$REG_TOKEN" \
    --name "$RUNNER_NAME" \
    --labels unreal \
    --unattended \
    --work _work \
    --replace)

echo "Installing systemd user service..."
cp "$SCRIPT_DIR/$SERVICE_NAME" "$SYSTEMD_DIR/$SERVICE_NAME"
systemctl --user daemon-reload
systemctl --user enable --now "$SERVICE_NAME"

echo "=== Runner Service Status ==="
systemctl --user status "$SERVICE_NAME" --no-pager

echo "=== Verified online runner ==="
gh api "repos/${REPO_SLUG}/actions/runners" --jq '.runners[] | {name, status, labels: [.labels[].name]}'
