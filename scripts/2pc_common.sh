#!/usr/bin/env bash

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "scripts/2pc_common.sh is a helper; source it from a run_2pc_*.sh script." >&2
    exit 2
fi

# Shared helpers for the per-protocol 2PC launchers.
#
# Timing convention:
#   - run_pair() records only controller/SSH wall-clock time in runs.csv.
#   - each endpoint binary writes CG_PROTOCOL_TIMING_FILE after it has completed
#     its protocol role in memory. Those party-owned timings exclude input
#     loading and output/check file writing, and are copied into
#     protocol_times.csv by the check_* helpers.
SCRIPT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOCAL_IP="${LOCAL_IP:-}"
REMOTE_IP="${REMOTE_IP:-}"
LOCAL_USER="${LOCAL_USER:-}"
REMOTE_USER="${REMOTE_USER:-}"
LOCAL_ROOT="${LOCAL_ROOT:-}"
REMOTE_ROOT="${REMOTE_ROOT:-}"

missing=()
[[ -n "$LOCAL_IP" ]] || missing+=(LOCAL_IP)
[[ -n "$REMOTE_IP" ]] || missing+=(REMOTE_IP)
[[ -n "$LOCAL_USER" ]] || missing+=(LOCAL_USER)
[[ -n "$REMOTE_USER" ]] || missing+=(REMOTE_USER)
[[ -n "$LOCAL_ROOT" ]] || missing+=(LOCAL_ROOT)
[[ -n "$REMOTE_ROOT" ]] || missing+=(REMOTE_ROOT)
if (( ${#missing[@]} )); then
    printf 'Missing required 2PC setting(s): %s\n' "${missing[*]}" >&2
    printf 'Set LOCAL_IP, REMOTE_IP, LOCAL_USER, REMOTE_USER, LOCAL_ROOT, and REMOTE_ROOT before running run_2pc_*.sh.\n' >&2
    exit 2
fi

LOCAL="${LOCAL_USER}@${LOCAL_IP}"
REMOTE="${REMOTE_USER}@${REMOTE_IP}"

BUILD_DIR="${BUILD_DIR:-build-2pc}"
CHECKER_DIR="${CHECKER_DIR:-$SCRIPT_ROOT/build-portable}"

CG_RF_LANES="${CG_RF_LANES:-8}"
CG_RF_THREADS_LOCAL="${CG_RF_THREADS_LOCAL:-28}"
CG_RF_THREADS_REMOTE="${CG_RF_THREADS_REMOTE:-32}"
CG_RF_CHUNK_SIZE="${CG_RF_CHUNK_SIZE:-128}"
CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-7200}"
CG_Q_NBITS="${CG_Q_NBITS:-128}"
CG_K="${CG_K:-1}"
CG_FIXED_Q="${CG_FIXED_Q:-170141183460469232709364739622490341377}"
CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}"
CG_PSI_INPUT_BITS="${CG_PSI_INPUT_BITS:-128}"
TIMEOUT_S="${TIMEOUT_S:-3600}"

CG_SESSION_ID="${CG_SESSION_ID:-$(openssl rand -hex 16)}"
CG_USE_NTT_POLY="${CG_USE_NTT_POLY:-1}"
CG_PUBLIC_SEED="${CG_PUBLIC_SEED:-4847948322520058953}"
CG_IO_TIMEOUT_S="${CG_IO_TIMEOUT_S:-180}"
CG_MAX_ITEMS="${CG_MAX_ITEMS:-1048576}"
REMOTE_SCRATCH="/tmp/cg_run_$CG_SESSION_ID"

common_env() {
    printf 'CG_RF_LANES=%q CG_RF_CHUNK_SIZE=%q CG_CONNECT_RETRIES=%q CG_Q_NBITS=%q CG_K=%q CG_FIXED_Q=%q CG_BENCH_INPUT_BITS=%q CG_PSI_INPUT_BITS=%q ' \
        "$CG_RF_LANES" "$CG_RF_CHUNK_SIZE" "$CG_CONNECT_RETRIES" "$CG_Q_NBITS" "$CG_K" "$CG_FIXED_Q" "$CG_BENCH_INPUT_BITS" "$CG_PSI_INPUT_BITS"
    printf 'CG_SESSION_ID=%q CG_USE_NTT_POLY=%q CG_PUBLIC_SEED=%q CG_IO_TIMEOUT_S=%q CG_MAX_ITEMS=%q CG_PROTOCOL_ID=%q CG_BIND_ADDR=%q ' \
        "$CG_SESSION_ID" "$CG_USE_NTT_POLY" "$CG_PUBLIC_SEED" "$CG_IO_TIMEOUT_S" "$CG_MAX_ITEMS" "${CG_PROTOCOL_ID:-manual}" "${CG_BIND_ADDR:-0.0.0.0}"
    printf 'CG_MAX_MPZ_BYTES=%q CG_VALIDATE_LANES=%q ' "${CG_MAX_MPZ_BYTES:-16384}" "${CG_VALIDATE_LANES:-0}"
}

next_power_of_two() {
    local n="$1"
    local p=1
    while (( p < n )); do
        p=$((p * 2))
    done
    printf '%s\n' "$p"
}

rf_psi_expected_opa_points() {
    local set_size="$1"
    local raw=$((2 * set_size + 1))
    if [[ "${CG_USE_NTT_POLY:-1}" != "0" && "$CG_FIXED_Q" == "170141183460469232709364739622490341377" ]]; then
        next_power_of_two "$raw"
    else
        printf '%s\n' "$raw"
    fi
}

ssh_local() {
    ssh -o BatchMode=yes "$LOCAL" "bash -lc $(printf '%q' "$1")"
}

ssh_remote() {
    ssh -o BatchMode=yes "$REMOTE" "bash -lc $(printf '%q' "$1")"
}

cleanup_ports() {
    : # Run-owned children are cleaned by remote shell traps, never fuser.
}

init_run_dir() {
    local prefix="$1"
    local stamp
    stamp="$(TZ=UTC date +%Y%m%d_%H%M%S_UTC)"
    CG_PROTOCOL_ID="$prefix"
    OUT_DIR="${OUT_DIR:-$SCRIPT_ROOT/${prefix}_${stamp}_$CG_SESSION_ID}"
    RAW_DIR="$OUT_DIR/raw"
    IO_DIR="$OUT_DIR/io"
    mkdir -p "$RAW_DIR" "$IO_DIR"
    {
        printf 'transport=plain_tcp\nvalidate_lanes=%s\n' "${CG_VALIDATE_LANES:-0}"
        printf 'direct_psi2_padding=zero\nrf_psi2_padding=zero\n'
        printf 'lanes=%s\nchunk=%s\nntt=%s\n' "$CG_RF_LANES" "$CG_RF_CHUNK_SIZE" "$CG_USE_NTT_POLY"
        printf 'local_threads=%s\nremote_threads=%s\n' "$CG_RF_THREADS_LOCAL" "$CG_RF_THREADS_REMOTE"
        printf 'psi2_local_threads=%s\npsi2_remote_threads=%s\n' "${CG_RF_THREADS_PSI2_LOCAL:-28}" "${CG_RF_THREADS_PSI2_REMOTE:-32}"
    } >"$OUT_DIR/run_config.txt"
    # CSV roles:
    #   runs.csv           controller launch time, not paper protocol time
    #   checks.csv         offline correctness checker status
    #   timings.csv        detailed phase timings parsed from logs
    #   protocol_times.csv party clocks from protocol start to role completion
    printf 'name,local_rc,remote_rc,outer_elapsed_s,logs\n' >"$OUT_DIR/runs.csv"
    printf 'name,checker_rc,checker_log\n' >"$OUT_DIR/checks.csv"
    printf 'run,side,component,metric,value_ms,value_kind,log,line\n' >"$OUT_DIR/timings.csv"
    printf 'run,side,component,protocol_time_ms,source\n' >"$OUT_DIR/protocol_times.csv"
}

run_pair() {
    local name="$1"
    local order="$2"
    local local_cmd="$3"
    local remote_cmd="$4"
    local local_log="$RAW_DIR/${name}_local.log"
    local remote_log="$RAW_DIR/${name}_remote.log"

    local_cmd="set -euo pipefail; ROOT='$LOCAL_ROOT'; source '$LOCAL_ROOT/scripts/run_common.sh'; export $(common_env) CG_RUN_LOG_DIR='$REMOTE_SCRATCH/logs'; cg_run_init lan; $local_cmd"
    remote_cmd="set -euo pipefail; ROOT='$REMOTE_ROOT'; source '$REMOTE_ROOT/scripts/run_common.sh'; export $(common_env) CG_RUN_LOG_DIR='$REMOTE_SCRATCH/logs'; cg_run_init lan; $remote_cmd"
    echo "==> $name"
    cleanup_ports
    local start end elapsed lr rr
    # Outer elapsed includes SSH/process overhead.  It remains useful for
    # run reproducibility but is deliberately separated from protocol time.
    start="$(date +%s%3N)"
    set +e
    if [[ "$order" == "local_first" ]]; then
        timeout "$TIMEOUT_S" ssh -o BatchMode=yes "$LOCAL" \
            "bash -lc $(printf '%q' "$local_cmd")" >"$local_log" 2>&1 &
        local lp=$!
        sleep 1
        timeout "$TIMEOUT_S" ssh -o BatchMode=yes "$REMOTE" \
            "bash -lc $(printf '%q' "$remote_cmd")" >"$remote_log" 2>&1
        rr=$?
        wait "$lp"
        lr=$?
    else
        timeout "$TIMEOUT_S" ssh -o BatchMode=yes "$REMOTE" \
            "bash -lc $(printf '%q' "$remote_cmd")" >"$remote_log" 2>&1 &
        local rp=$!
        sleep 1
        timeout "$TIMEOUT_S" ssh -o BatchMode=yes "$LOCAL" \
            "bash -lc $(printf '%q' "$local_cmd")" >"$local_log" 2>&1
        lr=$?
        wait "$rp"
        rr=$?
    fi
    set -e

    end="$(date +%s%3N)"
    elapsed="$(awk "BEGIN { printf \"%.3f\", ($end - $start) / 1000 }")"
    python3 - "$OUT_DIR/runs.csv" "$name" "$lr" "$rr" "$elapsed" "$local_log,$remote_log" <<'PYCSV'
import csv
import sys
with open(sys.argv[1], "a", newline="") as stream:
    csv.writer(stream).writerow(sys.argv[2:])
PYCSV
    printf '%s,controller,controller,outer elapsed,%s,elapsed,%s,%s\n' \
        "$name" "$(awk "BEGIN { printf \"%.3f\", $elapsed * 1000 }")" "$local_log" 0 >>"$OUT_DIR/timings.csv"
    python3 "$SCRIPT_ROOT/extract_timings.py" "$name" local "$local_log" "$OUT_DIR/timings.csv"
    python3 "$SCRIPT_ROOT/extract_timings.py" "$name" remote "$remote_log" "$OUT_DIR/timings.csv"
    if (( lr != 0 || rr != 0 )); then
        echo "FAILED $name local=$lr remote=$rr elapsed=${elapsed}s"
        echo "Local log:  $local_log"
        echo "Remote log: $remote_log"
        return 1
    fi
    echo "OK $name elapsed=${elapsed}s"
}

fetch_local() {
    ssh_local "cat '$1'" >"$2"
}

fetch_remote() {
    ssh_remote "cat '$1'" >"$2"
}

record_protocol_time() {
    local name="$1" file="$2" side="${3:-local}"
    [[ -f "$file" ]] || return 0
    local component value
    component="$(awk -F, 'NR==2 {print $1}' "$file")"
    value="$(awk -F, 'NR==2 {print $3}' "$file")"
    [[ -n "$component" && -n "$value" ]] || return 0
    # One normalized row per party-owned clock.  File writing and checker work
    # are outside the measured interval; the timing CSV is written afterward.
    printf '%s,%s,%s,%s,%s\n' "$name" "$side" "$component" "$value" "$file" >>"$OUT_DIR/protocol_times.csv"
    printf '%s,%s,%s,party protocol time,%s,party_clock,%s,%s\n' \
        "$name" "$side" "$component" "$value" "$file" 2 >>"$OUT_DIR/timings.csv"
}

fetch_protocol_time() {
    local local_dir="$1" out="$2" filename="${3:-protocol_time.csv}" name="${4:-}"
    set +e
    fetch_local "$local_dir/$filename" "$out/$filename" 2>/dev/null
    local timing_rc=$?
    set -e
    if (( timing_rc == 0 )); then
        cat "$out/$filename"
        if [[ -n "$name" ]]; then
            record_protocol_time "$name" "$out/$filename" local
        fi
    else
        rm -f "$out/$filename"
    fi
}

fetch_remote_protocol_time() {
    local remote_dir="$1" out="$2" filename="$3" name="${4:-}"
    [[ -n "$remote_dir" ]] || return 0
    local out_filename="remote_${filename}"
    if [[ "$filename" != "protocol_time.csv" ]]; then
        out_filename="$filename"
    fi
    set +e
    fetch_remote "$remote_dir/$filename" "$out/$out_filename" 2>/dev/null
    local timing_rc=$?
    set -e
    if (( timing_rc == 0 )); then
        cat "$out/$out_filename"
        if [[ -n "$name" ]]; then
            record_protocol_time "$name" "$out/$out_filename" remote
        fi
    else
        rm -f "$out/$out_filename"
    fi
}

check_batch_ole() {
    local name="$1" local_dir="$2" remote_dir="$3" sender_file="$4" receiver_file="$5"
    local sender_blinds="${6:-}" receiver_blinds="${7:-}"
    local out="$IO_DIR/$name"
    mkdir -p "$out"
    fetch_remote "$remote_dir/$sender_file" "$out/$sender_file"
    fetch_local "$local_dir/$receiver_file" "$out/$receiver_file"
    local checker_args=("$out/$sender_file" "$out/$receiver_file")
    if [[ -n "$sender_blinds" && -n "$receiver_blinds" ]]; then
        fetch_remote "$remote_dir/$sender_blinds" "$out/$sender_blinds"
        fetch_local "$local_dir/$receiver_blinds" "$out/$receiver_blinds"
        checker_args+=("$out/$sender_blinds" "$out/$receiver_blinds")
    fi
    fetch_protocol_time "$local_dir" "$out" protocol_time.csv "$name"
    fetch_remote_protocol_time "$remote_dir" "$out" protocol_time.csv "$name"
    set +e
    "$CHECKER_DIR/cg_check_batch_ole" "${checker_args[@]}" >"$out/check.log" 2>&1
    local rc=$?
    set -e
    echo "$name,$rc,$out/check.log" >>"$OUT_DIR/checks.csv"
    cat "$out/check.log"
    return "$rc"
}

check_ope() {
    local name="$1" local_dir="$2" remote_dir="$3" sender_file="$4" receiver_file="$5"
    local out="$IO_DIR/$name"
    mkdir -p "$out"
    fetch_remote "$remote_dir/$sender_file" "$out/$sender_file"
    fetch_local "$local_dir/$receiver_file" "$out/$receiver_file"
    fetch_protocol_time "$local_dir" "$out" protocol_time.csv "$name"
    fetch_remote_protocol_time "$remote_dir" "$out" protocol_time.csv "$name"
    set +e
    "$CHECKER_DIR/cg_check_ope" "$out/$sender_file" "$out/$receiver_file" >"$out/check.log" 2>&1
    local rc=$?
    set -e
    echo "$name,$rc,$out/check.log" >>"$OUT_DIR/checks.csv"
    cat "$out/check.log"
    return "$rc"
}

check_opa() {
    local name="$1" local_dir="$2" remote_dir="$3" sender_file="$4" receiver_file="$5"
    local out="$IO_DIR/$name"
    mkdir -p "$out"
    fetch_remote "$remote_dir/$sender_file" "$out/$sender_file"
    fetch_local "$local_dir/$receiver_file" "$out/$receiver_file"
    fetch_protocol_time "$local_dir" "$out" protocol_time.csv "$name"
    fetch_remote_protocol_time "$remote_dir" "$out" protocol_time.csv "$name"
    set +e
    "$CHECKER_DIR/cg_check_opa" "$out/$sender_file" "$out/$receiver_file" >"$out/check.log" 2>&1
    local rc=$?
    set -e
    echo "$name,$rc,$out/check.log" >>"$OUT_DIR/checks.csv"
    cat "$out/check.log"
    return "$rc"
}

check_psi() {
    local name="$1" local_dir="$2" remote_dir="${3:-}"
    local out="$IO_DIR/$name"
    mkdir -p "$out"
    fetch_local "$local_dir/true_intersection.txt" "$out/true_intersection.txt"
    fetch_local "$local_dir/intersection_out.txt" "$out/intersection_out.txt"
    fetch_protocol_time "$local_dir" "$out" protocol_time.csv "$name"
    fetch_remote_protocol_time "$remote_dir" "$out" protocol_time.csv "$name"
    set +e
    python3 "$SCRIPT_ROOT/check_correctness.py" \
        "$out/true_intersection.txt" "$out/intersection_out.txt" >"$out/check.log" 2>&1
    local rc=$?
    set -e
    echo "$name,$rc,$out/check.log" >>"$OUT_DIR/checks.csv"
    cat "$out/check.log"
    return "$rc"
}

check_psi2() {
    local name="$1" local_dir="$2" remote_dir="${3:-}"
    local out="$IO_DIR/$name"
    mkdir -p "$out"
    fetch_local "$local_dir/true_intersection.txt" "$out/true_intersection.txt"
    fetch_remote "$REMOTE_SCRATCH/logs/psi2_A.log" "$out/psi2_A.log"
    fetch_local "$REMOTE_SCRATCH/logs/psi2_B.log" "$out/psi2_B.log"
    # The human-readable party logs print only the first few intersection
    # elements for large sets.  Fetch the complete output files for correctness.
    fetch_remote "$REMOTE_SCRATCH/logs/psi2_A_intersection.txt" "$out/psi2_A_intersection.txt"
    fetch_local "$REMOTE_SCRATCH/logs/psi2_B_intersection.txt" "$out/psi2_B_intersection.txt"
    fetch_protocol_time "$local_dir" "$out" "protocol_time_B.csv" "$name"
    if [[ -n "$remote_dir" ]]; then
        fetch_remote_protocol_time "$remote_dir" "$out" "protocol_time_A.csv" "$name"
    fi
    set +e
    python3 "$SCRIPT_ROOT/check_psi2_correctness.py" \
        "$out/true_intersection.txt" \
        "$out/psi2_A_intersection.txt" \
        "$out/psi2_B_intersection.txt" >"$out/check.log" 2>&1
    local rc=$?
    set -e
    echo "$name,$rc,$out/check.log" >>"$OUT_DIR/checks.csv"
    cat "$out/check.log"
    return "$rc"
}

check_direct_psi2() {
    local name="$1" local_dir="$2" remote_dir="${3:-}"
    local out="$IO_DIR/$name"
    mkdir -p "$out"
    fetch_local "$local_dir/true_intersection.txt" "$out/true_intersection.txt"
    fetch_local "$local_dir/intersection_out.txt" "$out/psi2_B_intersection.txt"
    fetch_remote "$remote_dir/sender_intersection_out.txt" "$out/psi2_A_intersection.txt"
    fetch_protocol_time "$local_dir" "$out" "protocol_time_B.csv" "$name"
    fetch_remote_protocol_time "$remote_dir" "$out" "protocol_time_A.csv" "$name"
    set +e
    python3 "$SCRIPT_ROOT/check_psi2_correctness.py" \
        "$out/true_intersection.txt" \
        "$out/psi2_A_intersection.txt" \
        "$out/psi2_B_intersection.txt" >"$out/check.log" 2>&1
    local rc=$?
    set -e
    echo "$name,$rc,$out/check.log" >>"$OUT_DIR/checks.csv"
    cat "$out/check.log"
    return "$rc"
}

finish_run() {
    echo "Logs: $OUT_DIR"
}
