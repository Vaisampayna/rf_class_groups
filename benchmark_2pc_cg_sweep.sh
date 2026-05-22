#!/usr/bin/env bash
set -euo pipefail

LOCAL_IP="${LOCAL_IP:-PARTY_B_IP}"
REMOTE_IP="${REMOTE_IP:-PARTY_A_IP}"
LOCAL_USER="${LOCAL_USER:-party_b_user}"
REMOTE_USER="${REMOTE_USER:-party_a_user}"
LOCAL="${LOCAL_USER}@${LOCAL_IP}"
REMOTE="${REMOTE_USER}@${REMOTE_IP}"

LOCAL_ROOT="${LOCAL_ROOT:-/path/to/party_b/reverse_firewall_cg}"
REMOTE_ROOT="${REMOTE_ROOT:-/path/to/party_a/reverse_firewall_cg}"
BUILD_DIR="${BUILD_DIR:-build-2pc}"
CHECKER_DIR="${CHECKER_DIR:-/path/to/controller/reverse_firewall_cg/build-portable}"

SIZES=("$@")
if [[ "$#" -eq 0 ]]; then
    SIZES=(100 1000 2000 5000)
fi

STAMP="$(TZ=UTC date +%Y%m%d_%H%M%S_UTC)"
OUT_DIR="${OUT_DIR:-/path/to/controller/reverse_firewall_cg/benchmark_2pc_cg_sweep_${STAMP}}"
RAW_DIR="$OUT_DIR/raw"
IO_DIR="$OUT_DIR/io"
mkdir -p "$RAW_DIR" "$IO_DIR"

CG_RF_LANES="${CG_RF_LANES:-16}"
CG_RF_THREADS_LOCAL="${CG_RF_THREADS_LOCAL:-28}"
CG_RF_THREADS_REMOTE="${CG_RF_THREADS_REMOTE:-32}"
CG_RF_CHUNK_SIZE="${CG_RF_CHUNK_SIZE:-128}"
CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-7200}"
CG_Q_NBITS="${CG_Q_NBITS:-128}"
CG_K="${CG_K:-1}"
CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}"
TIMEOUT_S="${TIMEOUT_S:-3600}"

common_env() {
    printf 'CG_RF_LANES=%q CG_RF_CHUNK_SIZE=%q CG_CONNECT_RETRIES=%q CG_Q_NBITS=%q CG_K=%q CG_BENCH_INPUT_BITS=%q ' \
        "$CG_RF_LANES" "$CG_RF_CHUNK_SIZE" "$CG_CONNECT_RETRIES" "$CG_Q_NBITS" "$CG_K" "$CG_BENCH_INPUT_BITS"
}

ssh_local() {
    ssh -o BatchMode=yes "$LOCAL" "bash -lc $(printf '%q' "$1")"
}

ssh_remote() {
    ssh -o BatchMode=yes "$REMOTE" "bash -lc $(printf '%q' "$1")"
}

cleanup_ports() {
    local ports=(9001 9002 9003 9004 9010 9021 9022 9023 9041 9042 9043 9103 9104)
    ssh_local "fuser -k ${ports[*]/%//tcp} >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
    ssh_remote "fuser -k ${ports[*]/%//tcp} >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
    sleep 1
}

record_protocol_time() {
    local name="$1"
    local side="$2"
    local src="$3"
    local dst="$4"
    local line component ms

    if [[ ! -s "$dst" ]]; then
        return 0
    fi
    line="$(awk -F, 'NF >= 3 && $1 != "component" { last = $0 } END { print last }' "$dst")"
    component="$(awk -F, '{print $1}' <<<"$line")"
    ms="$(awk -F, '{print $3}' <<<"$line")"
    if [[ -n "$component" && -n "$ms" ]]; then
        printf '%s,%s,%s,%s,%s\n' "$name" "$side" "$component" "$ms" "$src" >>"$OUT_DIR/protocol_times.csv"
    fi
}

collect_protocol_times() {
    local name="$1"
    local local_timing="$2"
    local remote_timing="$3"
    local out="$IO_DIR/$name"

    mkdir -p "$out"
    if ssh_local "test -s '$local_timing'" >/dev/null 2>&1; then
        fetch_local "$local_timing" "$out/protocol_time.csv"
        record_protocol_time "$name" "local" "$out/protocol_time.csv" "$out/protocol_time.csv"
    fi
    if ssh_remote "test -s '$remote_timing'" >/dev/null 2>&1; then
        fetch_remote "$remote_timing" "$out/remote_protocol_time.csv"
        record_protocol_time "$name" "remote" "$out/remote_protocol_time.csv" "$out/remote_protocol_time.csv"
    fi
}

run_pair() {
    local name="$1"
    local order="$2"
    local local_cmd="$3"
    local remote_cmd="$4"
    local local_log="$RAW_DIR/${name}_local.log"
    local remote_log="$RAW_DIR/${name}_remote.log"
    local local_timing="/tmp/cg_protocol_time_${name}_local.csv"
    local remote_timing="/tmp/cg_protocol_time_${name}_remote.csv"

    echo "==> $name"
    cleanup_ports
    ssh_local "rm -f '$local_timing'" >/dev/null 2>&1 || true
    ssh_remote "rm -f '$remote_timing'" >/dev/null 2>&1 || true
    local start end elapsed lr rr
    start="$(date +%s%3N)"
    set +e
    if [[ "$order" == "local_first" ]]; then
        timeout "$TIMEOUT_S" ssh -o BatchMode=yes "$LOCAL" \
            "bash -lc $(printf '%q' "export CG_PROTOCOL_TIMING_FILE='$local_timing'; $local_cmd")" >"$local_log" 2>&1 &
        local lp=$!
        sleep 1
        timeout "$TIMEOUT_S" ssh -o BatchMode=yes "$REMOTE" \
            "bash -lc $(printf '%q' "export CG_PROTOCOL_TIMING_FILE='$remote_timing'; $remote_cmd")" >"$remote_log" 2>&1
        rr=$?
        wait "$lp"
        lr=$?
    else
        timeout "$TIMEOUT_S" ssh -o BatchMode=yes "$REMOTE" \
            "bash -lc $(printf '%q' "export CG_PROTOCOL_TIMING_FILE='$remote_timing'; $remote_cmd")" >"$remote_log" 2>&1 &
        local rp=$!
        sleep 1
        timeout "$TIMEOUT_S" ssh -o BatchMode=yes "$LOCAL" \
            "bash -lc $(printf '%q' "export CG_PROTOCOL_TIMING_FILE='$local_timing'; $local_cmd")" >"$local_log" 2>&1
        lr=$?
        wait "$rp"
        rr=$?
    fi
    set -e
    end="$(date +%s%3N)"
    elapsed="$(awk "BEGIN { printf \"%.3f\", ($end - $start) / 1000 }")"
    printf '%s,%s,%s,%s,%s\n' "$name" "$lr" "$rr" "$elapsed" "$local_log,$remote_log" >>"$OUT_DIR/runs.csv"
    if (( lr != 0 || rr != 0 )); then
        echo "FAILED $name local=$lr remote=$rr elapsed=${elapsed}s"
        return 1
    fi
    collect_protocol_times "$name" "$local_timing" "$remote_timing"
    echo "OK $name elapsed=${elapsed}s"
}

seq_args() {
    local first="$1" count="$2"
    seq "$first" "$((first + count - 1))" | paste -sd' ' -
}

bench_input_args() {
    local count="$1" domain="$2"
    python3 - "$count" "$domain" "$CG_BENCH_INPUT_BITS" <<'PY'
import sys
count = int(sys.argv[1])
domain = int(sys.argv[2], 0)
bits = int(sys.argv[3])
mod = 1 << bits
state = (0x9e3779b97f4a7c15 ^ (domain * 0xbf58476d1ce4e5b9)) & ((1 << 64) - 1)
out = []
for _ in range(count):
    state = (state + 0x9e3779b97f4a7c15) & ((1 << 64) - 1)
    z = state
    z = ((z ^ (z >> 30)) * 0xbf58476d1ce4e5b9) & ((1 << 64) - 1)
    z = ((z ^ (z >> 27)) * 0x94d049bb133111eb) & ((1 << 64) - 1)
    z ^= z >> 31
    out.append(str((z % (mod - 1)) + 1))
print(" ".join(out))
PY
}

fetch_local() {
    ssh_local "cat '$1'" >"$2"
}

fetch_remote() {
    ssh_remote "cat '$1'" >"$2"
}

check_batch_ole() {
    local name="$1" local_dir="$2" remote_dir="$3" sender_file="$4" receiver_file="$5"
    local out="$IO_DIR/$name"
    mkdir -p "$out"
    fetch_remote "$remote_dir/$sender_file" "$out/$sender_file"
    fetch_local "$local_dir/$receiver_file" "$out/$receiver_file"
    set +e
    "$CHECKER_DIR/cg_check_batch_ole" "$out/$sender_file" "$out/$receiver_file" >"$out/check.log" 2>&1
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
    set +e
    "$CHECKER_DIR/cg_check_ope" "$out/$sender_file" "$out/$receiver_file" >"$out/check.log" 2>&1
    local rc=$?
    set -e
    echo "$name,$rc,$out/check.log" >>"$OUT_DIR/checks.csv"
    cat "$out/check.log"
    return "$rc"
}

printf 'name,local_rc,remote_rc,outer_elapsed_s,logs\n' >"$OUT_DIR/runs.csv"
printf 'name,checker_rc,checker_log\n' >"$OUT_DIR/checks.csv"
printf 'run,side,component,protocol_time_ms,source\n' >"$OUT_DIR/protocol_times.csv"

ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"

for N in "${SIZES[@]}"; do
    LOCAL_IO="/tmp/cg_direct_batched_ole_${N}"
    REMOTE_IO="/tmp/cg_direct_batched_ole_${N}"
    run_pair "direct_batched_ole_${N}" "local_first" \
        "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT/$BUILD_DIR' && $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO' ./cg_batch_ole_receiver '$N' 9003" \
        "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT/$BUILD_DIR' && $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' ./cg_batch_ole_sender '$N' '$LOCAL_IP' 9003"
    check_batch_ole "direct_batched_ole_${N}" "$LOCAL_IO" "$REMOTE_IO" \
        direct_ole_sender_inputs.txt direct_ole_receiver_io.txt

    LOCAL_IO="/tmp/cg_rf_batched_ole_${N}"
    REMOTE_IO="/tmp/cg_rf_batched_ole_${N}"
    run_pair "rf_batched_ole_${N}" "local_first" \
        "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT' && CG_BUILD_DIR='$LOCAL_ROOT/$BUILD_DIR' $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO' bash ./run_rf_cg_batch_ole_lan_receiver.sh '$N'" \
        "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT' && CG_BUILD_DIR='$REMOTE_ROOT/$BUILD_DIR' $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' bash ./run_rf_cg_batch_ole_lan_sender.sh '$LOCAL_IP' '$N'"
    check_batch_ole "rf_batched_ole_${N}" "$LOCAL_IO" "$REMOTE_IO" \
        rf_ole_sender_inputs.txt rf_ole_receiver_io.txt

    LOCAL_IO="/tmp/cg_direct_ope_${N}"
    REMOTE_IO="/tmp/cg_direct_ope_${N}"
    run_pair "direct_ope_${N}" "local_first" \
        "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT/$BUILD_DIR' && $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO' ./cg_ope_receiver '$N' 9003" \
        "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT/$BUILD_DIR' && $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' ./cg_ope_sender '$LOCAL_IP' '$N' 9003"
    check_ope "direct_ope_${N}" "$LOCAL_IO" "$REMOTE_IO" \
        direct_ope_sender_coeffs.txt direct_ope_receiver_output.txt

    LOCAL_IO="/tmp/cg_rf_ope_${N}"
    REMOTE_IO="/tmp/cg_rf_ope_${N}"
    run_pair "rf_ope_${N}" "local_first" \
        "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT' && CG_BUILD_DIR='$LOCAL_ROOT/$BUILD_DIR' $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO' bash ./run_rf_cg_ope_lan_receiver.sh '$N'" \
        "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT' && CG_BUILD_DIR='$REMOTE_ROOT/$BUILD_DIR' $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' bash ./run_rf_cg_ope_lan_sender.sh '$LOCAL_IP' '$N'"
    check_ope "rf_ope_${N}" "$LOCAL_IO" "$REMOTE_IO" \
        rf_ope_sender_coeffs.txt rf_ope_receiver_output.txt

    run_pair "direct_opa_${N}" "local_first" \
        "cd '$LOCAL_ROOT/$BUILD_DIR' && export $ENV_LOCAL; ./cg_opa_receiver '$N' '$N' --bench64 0x4f50415f42 9103" \
        "cd '$REMOTE_ROOT/$BUILD_DIR' && export $ENV_REMOTE; ./cg_opa_sender '$LOCAL_IP' '$N' '$N' --bench64 0x4f50415f42 -- --bench64 0x4f50415f41 9103"

    run_pair "rf_opa_${N}" "local_first" \
        "cd '$LOCAL_ROOT/$BUILD_DIR'; export $ENV_LOCAL; ./cg_rf_opa_receiver '$N' '$N' --bench64 0x4f50415f42 & ./cg_rf_receiver_firewall_opa; wait" \
        "cd '$REMOTE_ROOT/$BUILD_DIR'; export $ENV_REMOTE; ./cg_rf_sender_firewall_opa '$LOCAL_IP' & sleep 1; ./cg_rf_opa_sender '$N' '$N' --bench64 0x4f50415f41 -- --bench64 0x4f50415f52; wait"

    run_pair "direct_psi_${N}" "local_first" \
        "cd '$LOCAL_ROOT/$BUILD_DIR' && export $ENV_LOCAL; ./cg_psi_receiver '$N' --random '$N' 42 --port 9103" \
        "cd '$REMOTE_ROOT/$BUILD_DIR' && export $ENV_REMOTE; ./cg_psi_sender '$LOCAL_IP' '$N' --random '$N' 42 --port 9103"

    run_pair "rf_psi_${N}" "local_first" \
        "cd '$LOCAL_ROOT' && CG_BUILD_DIR='$LOCAL_ROOT/$BUILD_DIR' $ENV_LOCAL bash ./run_rf_cg_psi_lan_receiver.sh --random '$N' '$N' 42" \
        "cd '$REMOTE_ROOT' && CG_BUILD_DIR='$REMOTE_ROOT/$BUILD_DIR' $ENV_REMOTE bash ./run_rf_cg_psi_lan_sender.sh '$LOCAL_IP' --random '$N' '$N' 42"
done

echo "Logs: $OUT_DIR"
