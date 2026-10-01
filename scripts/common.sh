#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
export UPROJECT="$PROJECT_ROOT/KiteSurf.uproject"

# Resolve UE_ROOT: $UE_ROOT -> .engine-path -> /opt/unreal-engine
if [[ -n "${UE_ROOT:-}" && -d "${UE_ROOT:-}" ]]; then
    :
elif [[ -f "$PROJECT_ROOT/.engine-path" ]]; then
    export UE_ROOT="$(cat "$PROJECT_ROOT/.engine-path" | tr -d '[:space:]')"
elif [[ -d "/opt/unreal-engine" ]]; then
    export UE_ROOT="/opt/unreal-engine"
else
    echo "ERROR: Unreal Engine installation not found. Set UE_ROOT or create .engine-path" >&2
    exit 1
fi

if [[ ! -d "$UE_ROOT" ]]; then
    echo "ERROR: Resolved UE_ROOT does not exist: $UE_ROOT" >&2
    exit 1
fi

export UE_BUILD="$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh"
export UE_RUNUAT="$UE_ROOT/Engine/Build/BatchFiles/Linux/RunUAT.sh"
export UE_EDITOR="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
export UE_EDITOR_CMD="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"
