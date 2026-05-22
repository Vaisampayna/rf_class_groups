#!/usr/bin/env bash
set -euo pipefail

LOCAL_IP="${LOCAL_IP:-PARTY_B_IP}"
REMOTE_IP="${REMOTE_IP:-PARTY_A_IP}"
REMOTE_USER="${REMOTE_USER:-party_a_user}"
REMOTE="${REMOTE_USER}@${REMOTE_IP}"

LOCAL_ROOT="${LOCAL_ROOT:-/path/to/party_b/reverse_firewall_cg}"
REMOTE_ROOT="${REMOTE_ROOT:-/path/to/party_a/reverse_firewall_cg}"
BUILD_DIR="${BUILD_DIR:-build-2pc}"
CHECKER_DIR="${CHECKER_DIR:-/path/to/controller/reverse_firewall_cg/build-portable}"

N="${1:-1000}"
STAMP="$(TZ=UTC date +%Y%m%d_%H%M%S_UTC)"
OUT_DIR="${OUT_DIR:-/path/to/controller/reverse_firewall_cg/benchmark_2pc_cg_${STAMP}}"
RAW_DIR="$OUT_DIR/raw"
IO_DIR="$OUT_DIR/io"
mkdir -p "$RAW_DIR"
mkdir -p "$IO_DIR"

CG_RF_LANES="${CG_RF_LANES:-12}"
CG_RF_THREADS_LOCAL="${CG_RF_THREADS_LOCAL:-28}"
CG_RF_THREADS_REMOTE="${CG_RF_THREADS_REMOTE:-28}"
CG_RF_CHUNK_SIZE="${CG_RF_CHUNK_SIZE:-128}"
CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-7200}"
CG_Q_NBITS="${CG_Q_NBITS:-128}"
CG_K="${CG_K:-1}"

common_env() {
    printf 'CG_RF_LANES=%q CG_RF_CHUNK_SIZE=%q CG_CONNECT_RETRIES=%q CG_Q_NBITS=%q CG_K=%q ' \
        "$CG_RF_LANES" "$CG_RF_CHUNK_SIZE" "$CG_CONNECT_RETRIES" "$CG_Q_NBITS" "$CG_K"
}

ssh_run() {
    ssh -o BatchMode=yes "$REMOTE" "bash -lc $(printf '%q' "$1")"
}

cleanup_ports() {
    local ports=(9001 9002 9003 9004 9010 9021 9022 9023)
    ssh -o BatchMode=yes party_b_user@"$LOCAL_IP" "fuser -k ${ports[*]/%//tcp} >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
    ssh_run "fuser -k ${ports[*]/%//tcp} >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
    sleep 1
}

run_pair() {
    local name="$1"
    local order="$2"
    local local_cmd="$3"
    local remote_cmd="$4"
    local local_log="$RAW_DIR/${name}_local.log"
    local remote_log="$RAW_DIR/${name}_remote.log"
    echo "==> $name"
    cleanup_ports
    local start end
    start="$(date +%s%3N)"
    set +e
    if [[ "$order" == "local_first" ]]; then
        ssh -o BatchMode=yes party_b_user@"$LOCAL_IP" "bash -lc $(printf '%q' "$local_cmd")" >"$local_log" 2>&1 &
        local lp=$!
        sleep 1
        ssh_run "$remote_cmd" >"$remote_log" 2>&1
        local rr=$?
        wait "$lp"
        local lr=$?
    else
        ssh_run "$remote_cmd" >"$remote_log" 2>&1 &
        local rp=$!
        sleep 1
        ssh -o BatchMode=yes party_b_user@"$LOCAL_IP" "bash -lc $(printf '%q' "$local_cmd")" >"$local_log" 2>&1
        local lr=$?
        wait "$rp"
        local rr=$?
    fi
    set -e
    end="$(date +%s%3N)"
    local elapsed
    elapsed="$(awk "BEGIN { printf \"%.3f\", ($end - $start) / 1000 }")"
    echo "$name,$lr,$rr,$elapsed,$local_log,$remote_log" >>"$OUT_DIR/runs.csv"
    if (( lr != 0 || rr != 0 )); then
        echo "FAILED $name local=$lr remote=$rr elapsed=${elapsed}s"
        return 1
    fi
    echo "OK $name elapsed=${elapsed}s"
}

seq_args() {
    local first="$1" count="$2"
    seq "$first" "$((first + count - 1))" | paste -sd' ' -
}

printf 'name,local_rc,remote_rc,outer_elapsed_s,local_log,remote_log\n' >"$OUT_DIR/runs.csv"
printf 'name,checker_rc,checker_log\n' >"$OUT_DIR/checks.csv"

ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"

fetch_local() {
    local src="$1" dst="$2"
    ssh -o BatchMode=yes party_b_user@"$LOCAL_IP" "cat '$src'" >"$dst"
}

fetch_remote() {
    local src="$1" dst="$2"
    ssh_run "cat '$src'" >"$dst"
}

check_batch_ole() {
    local name="$1" local_dir="$2" remote_dir="$3" sender_file="$4" receiver_file="$5"
    local out="$IO_DIR/$name"
    mkdir -p "$out"
    fetch_remote "$remote_dir/$sender_file" "$out/$sender_file"
    fetch_local "$local_dir/$receiver_file" "$out/$receiver_file"
    set +e
    "$CHECKER_DIR/cg_check_batch_ole" "$out/$sender_file" "$out/$receiver_file" \
        >"$out/check.log" 2>&1
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
    "$CHECKER_DIR/cg_check_ope" "$out/$sender_file" "$out/$receiver_file" \
        >"$out/check.log" 2>&1
    local rc=$?
    set -e
    echo "$name,$rc,$out/check.log" >>"$OUT_DIR/checks.csv"
    cat "$out/check.log"
    return "$rc"
}

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

coeff_b="$(seq_args 3 $((N + 1)))"
coeff_a="$(seq_args 1 $((N + 1)))"
coeff_r="$(seq_args 7 $((N + 1)))"
run_pair "rf_opa_${N}" "local_first" \
    "cd '$LOCAL_ROOT/$BUILD_DIR'; export $ENV_LOCAL; ./cg_rf_opa_receiver '$N' '$N' $coeff_b & ./cg_rf_receiver_firewall_opa; wait" \
    "cd '$REMOTE_ROOT/$BUILD_DIR'; export $ENV_REMOTE; ./cg_rf_sender_firewall_opa '$LOCAL_IP' & sleep 1; ./cg_rf_opa_sender '$N' '$N' $coeff_a -- $coeff_r; wait"

run_pair "rf_psi_${N}" "local_first" \
    "cd '$LOCAL_ROOT' && CG_BUILD_DIR='$LOCAL_ROOT/$BUILD_DIR' $ENV_LOCAL bash ./run_rf_cg_psi_lan_receiver.sh --random '$N' '$N' 42" \
    "cd '$REMOTE_ROOT' && CG_BUILD_DIR='$REMOTE_ROOT/$BUILD_DIR' $ENV_REMOTE bash ./run_rf_cg_psi_lan_sender.sh '$LOCAL_IP' --random '$N' '$N' 42"

LOCAL_IO="/tmp/cg_direct_ope_${N}"
REMOTE_IO="/tmp/cg_direct_ope_${N}"
run_pair "direct_ope_${N}" "local_first" \
    "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT/$BUILD_DIR' && $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO' ./cg_ope_receiver '$N' 9003" \
    "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT/$BUILD_DIR' && $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' ./cg_ope_sender '$LOCAL_IP' '$N' 9003"
check_ope "direct_ope_${N}" "$LOCAL_IO" "$REMOTE_IO" \
    direct_ope_sender_coeffs.txt direct_ope_receiver_output.txt

ope_coeffs="$(seq_args 1 $((N + 1)))"
LOCAL_IO="/tmp/cg_rf_ope_${N}"
REMOTE_IO="/tmp/cg_rf_ope_${N}"
run_pair "rf_ope_${N}" "local_first" \
    "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT/$BUILD_DIR'; export $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO'; ./cg_rf_ope_receiver '$N' & ./cg_rf_receiver_firewall_opa; wait" \
    "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT/$BUILD_DIR'; export $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO'; ./cg_rf_sender_firewall_opa '$LOCAL_IP' & sleep 1; ./cg_rf_ope_sender '$LOCAL_IP' '$N' $ope_coeffs; wait"
check_ope "rf_ope_${N}" "$LOCAL_IO" "$REMOTE_IO" \
    rf_ope_sender_coeffs.txt rf_ope_receiver_output.txt

PSI2_ENV_LOCAL="$(common_env) CG_RF_THREADS=10"
PSI2_ENV_REMOTE="$(common_env) CG_RF_THREADS=12"
run_pair "rf_psi2_exact_${N}" "local_first" \
    "cd '$LOCAL_ROOT' && CG_BUILD_DIR='$LOCAL_ROOT/$BUILD_DIR' $PSI2_ENV_LOCAL bash ./run_rf_cg_psi2_lan_b.sh '$REMOTE_IP' --random '$N' '$N' 42" \
    "cd '$REMOTE_ROOT' && CG_BUILD_DIR='$REMOTE_ROOT/$BUILD_DIR' $PSI2_ENV_REMOTE bash ./run_rf_cg_psi2_lan_a.sh '$LOCAL_IP' --random '$N' '$N' 42"

echo "Logs: $OUT_DIR"
