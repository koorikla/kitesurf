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
export UE_RUNUAT="$UE_ROOT/Engine/Build/BatchFiles/RunUAT.sh"
export UE_EDITOR="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor"
export UE_EDITOR_CMD="$UE_ROOT/Engine/Binaries/Linux/UnrealEditor-Cmd"

# One GPU (8 GB) is shared by every worktree, agent and the CI runner on this machine, and two
# Vulkan runs at once can run it out of memory. with_gpu_lock runs a command while holding a
# machine-wide flock, so GPU runs queue instead of crashing. The lock lives outside any worktree
# and is held by flock itself (-o), so a leftover child of the command cannot keep it.
#   KITESURF_GPU_LOCK=0          run without the lock
#   KITESURF_GPU_LOCK_FILE=path  use another lock file
gpu_lock_file() {
    local dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
    [[ -d "$dir" ]] || dir=/tmp
    echo "${KITESURF_GPU_LOCK_FILE:-$dir/kitesurf-gpu.lock}"
}

with_gpu_lock() {
    if [[ "${KITESURF_GPU_LOCK:-1}" == 0 ]]; then
        "$@"
        return
    fi
    local lock start=""
    lock="$(gpu_lock_file)"
    if ! flock -n "$lock" true; then
        start=$(date +%s)
        echo "=== Waiting for the GPU lock $lock, held by: $(cat "$lock" 2>/dev/null || echo unknown) ==="
        echo "    (KITESURF_GPU_LOCK=0 skips the lock)"
    fi
    flock -o "$lock" bash -c '
        lock="$1" project="$2" start="$3"; shift 3
        [[ -n "$start" ]] && echo "=== Got the GPU lock after $(($(date +%s) - start))s ==="
        echo "pid $$ $(basename "$1") from $project since $(date "+%F %T")" > "$lock"
        exec "$@"' with_gpu_lock "$lock" "$PROJECT_ROOT" "${start:-}" "$@"
}

# True if an engine argument is present; engine arguments ignore case.
has_arg() {
    local want="${1,,}" arg
    shift
    for arg in "$@"; do
        [[ "${arg,,}" == "$want" ]] && return 0
    done
    return 1
}
