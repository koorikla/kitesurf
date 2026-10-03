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
# Not every GPU user takes the lock: a branch from before it, a direct engine call or a windowed
# editor. So once the lock is held, the command also waits until the GPU has free VRAM.
#   KITESURF_GPU_MIN_FREE_MB=4096   free VRAM to wait for; 0 skips the wait
#   KITESURF_GPU_WAIT_SECONDS=900   stop waiting after this long and start anyway
gpu_lock_file() {
    local dir="${XDG_RUNTIME_DIR:-/run/user/$(id -u)}"
    [[ -d "$dir" ]] || dir=/tmp
    echo "${KITESURF_GPU_LOCK_FILE:-$dir/kitesurf-gpu.lock}"
}

gpu_free_mb() {
    nvidia-smi --query-gpu=memory.free --format=csv,noheader,nounits 2>/dev/null | head -n 1 | tr -d ' '
}

# Engine processes that use the GPU, for the log of whoever is waiting on them.
gpu_engine_processes() {
    pgrep -a '^UnrealEditor' | grep -vi -- '-nullrhi' | cut -c1-240 || true
}

wait_for_free_vram() {
    local need="${KITESURF_GPU_MIN_FREE_MB:-4096}" limit="${KITESURF_GPU_WAIT_SECONDS:-900}"
    local start=$SECONDS free waited=""
    [[ "$need" -gt 0 ]] && command -v nvidia-smi >/dev/null || return 0
    while free="$(gpu_free_mb)" && [[ "$free" =~ ^[0-9]+$ ]] && ((free < need)); do
        if [[ -z "$waited" ]]; then
            waited=1
            echo "=== Waiting for ${need} MB of free VRAM, ${free} MB free. Engine processes: ==="
            gpu_engine_processes | sed 's/^/    /'
            echo "    (KITESURF_GPU_MIN_FREE_MB=0 skips the wait)"
        fi
        if ((SECONDS - start >= limit)); then
            echo "=== Still ${free} MB free after ${limit}s; starting anyway ==="
            return 0
        fi
        sleep 5
    done
    [[ -n "$waited" ]] && echo "=== ${free} MB of VRAM free after $((SECONDS - start))s ==="
    return 0
}
export -f gpu_free_mb gpu_engine_processes wait_for_free_vram

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
        wait_for_free_vram
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
