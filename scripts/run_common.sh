#!/usr/bin/env bash
# Shared run ownership, log directories, and bounded child waits.
cg_run_init() {
    if [[ "${CG_RUN_INITIALIZED:-0}" == 1 ]]; then
        trap 'cg_cleanup_children' EXIT
        trap 'exit 130' INT
        trap 'exit 143' TERM HUP
        return 0
    fi
    local mode="${1:-local}"
    export CG_SESSION_ID="${CG_SESSION_ID:-$(openssl rand -hex 16)}"
    if [[ "$mode" == local ]]; then
        export CG_BIND_ADDR="${CG_BIND_ADDR:-127.0.0.1}"
    else
        export CG_BIND_ADDR="${CG_BIND_ADDR:-0.0.0.0}"
    fi
    [[ "$CG_SESSION_ID" =~ ^[a-zA-Z0-9_-]{1,64}$ ]] || return 2
    export CG_PROTOCOL_ID="${CG_PROTOCOL_ID:-manual}"
    export CG_IO_TIMEOUT_S="${CG_IO_TIMEOUT_S:-180}"
    export CG_RUN_LOG_DIR="${CG_RUN_LOG_DIR:-$ROOT/logs/run_$CG_SESSION_ID}"
    export CG_PSI_INPUT_DIR="${CG_PSI_INPUT_DIR:-$CG_RUN_LOG_DIR/inputs}"
    export CG_BENCH_IO_DIR="${CG_BENCH_IO_DIR:-$CG_RUN_LOG_DIR/io}"
    mkdir -p "$CG_RUN_LOG_DIR" "$CG_BENCH_IO_DIR"
    # Never kill the owner of a port. Serialize default port use on each host.
    # For disjoint port maps a caller may supply a different lock pathname.
    exec {CG_LOCK_FD}>"${CG_RUN_LOCK:-/tmp/rf-class-groups.lock}"
    if ! flock -n "$CG_LOCK_FD"; then
        echo 'Another benchmark owns this port map; use disjoint ports and CG_RUN_LOCK.' >&2
        return 2
    fi
    export CG_RUN_INITIALIZED=1
    trap 'cg_cleanup_children' EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM HUP
}

cg_cleanup_children() {
    local pid
    for pid in $(jobs -pr); do kill "$pid" 2>/dev/null || true; done
    wait 2>/dev/null || true
}

cg_wait_child() {
    cg_wait_all "$1"
}

cg_wait_all() {
    local pids=("$@") limit="${CG_CHILD_TIMEOUT_S:-${TIMEOUT_S:-3600}}"
    [[ "$limit" =~ ^[1-9][0-9]*$ ]] || return 2
    local deadline=$((SECONDS + limit)) status pid
    while (( ${#pids[@]} )); do
        local remaining=()
        for pid in "${pids[@]}"; do
            if kill -0 "$pid" 2>/dev/null; then
                remaining+=("$pid")
            else
                status=0
                wait "$pid" || status=$?
                if (( status != 0 )); then
                    for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
                    wait 2>/dev/null || true
                    return "$status"
                fi
            fi
        done
        pids=("${remaining[@]}")
        if (( SECONDS >= deadline )); then
            for pid in "${pids[@]}"; do kill "$pid" 2>/dev/null || true; done
            wait 2>/dev/null || true
            return 124
        fi
        (( ${#pids[@]} == 0 )) || sleep 0.05
    done
}

cg_parallel() {
    local commands=("$@") pids=() command
    for command in "${commands[@]}"; do
        bash -euo pipefail -c "$command" &
        pids+=("$!")
    done
    cg_wait_all "${pids[@]}"
}

cg_positive_size() {
    [[ "$1" =~ ^[1-9][0-9]*$ && ${#1} -le 8 ]] && (( 10#$1 <= ${CG_MAX_ITEMS:-1048576} ))
}
