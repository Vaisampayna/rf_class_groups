#!/usr/bin/env bash
set -euo pipefail

# Sweep direct three-round OLE and reverse-firewalled three-round OLE on the
# two-machine setup.  This script is separate from benchmark_2pc_cg_sweep.sh so
# missing OLE3 rows can be regenerated without changing the main table.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

SIZES=("$@")
if [[ "$#" -eq 0 ]]; then
    SIZES=(1024 2048 4096 8192 16384 32768)
fi

init_run_dir "benchmark_2pc_ole3_sweep"

ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"
FAILED=0

run_and_check() {
    local name="$1"
    local local_cmd="$2"
    local remote_cmd="$3"
    shift 3

    set +e
    run_pair "$name" "local_first" "$local_cmd" "$remote_cmd"
    local run_rc=$?
    if (( run_rc == 0 )); then
        "$@"
        run_rc=$?
    fi
    set -e

    if (( run_rc != 0 )); then
        FAILED=1
        echo "WARNING: $name returned rc=${run_rc}; continuing with next run." >&2
    fi
}

for N in "${SIZES[@]}"; do
    LOCAL_IO="/tmp/cg_direct_ole3_${N}"
    REMOTE_IO="/tmp/cg_direct_ole3_${N}"
    ssh_local "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'"
    ssh_remote "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'"
    run_and_check "direct_ole3_${N}" \
        "cd '$LOCAL_ROOT/$BUILD_DIR'; export $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO' CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time.csv'; ./cg_ole3_receiver '$N' 9003" \
        "cd '$REMOTE_ROOT/$BUILD_DIR'; export $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time.csv'; ./cg_ole3_sender '$N' '$LOCAL_IP' 9003" \
        check_batch_ole "direct_ole3_${N}" "$LOCAL_IO" "$REMOTE_IO" \
            ole3_sender_inputs.txt ole3_receiver_io.txt

    LOCAL_IO="/tmp/cg_rf_ole3_${N}"
    REMOTE_IO="/tmp/cg_rf_ole3_${N}"
    ssh_local "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'"
    ssh_remote "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'"
    run_and_check "rf_ole3_${N}" \
        "cd '$LOCAL_ROOT/$BUILD_DIR'; export $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO' CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time.csv'; ./cg_rf_ole3_receiver '$N' & ./cg_rf_receiver_firewall_ole3; wait" \
        "cd '$REMOTE_ROOT/$BUILD_DIR'; export $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time.csv'; ./cg_rf_sender_firewall_ole3 '$LOCAL_IP' & sleep 1; ./cg_rf_ole3_sender '$N'; wait" \
        check_batch_ole "rf_ole3_${N}" "$LOCAL_IO" "$REMOTE_IO" \
            rf_ole3_sender_inputs.txt rf_ole3_receiver_io.txt \
            rf_ole3_sender_firewall_blinds.txt rf_ole3_receiver_firewall_blinds.txt
done

finish_run
echo "OLE3 party timings: $OUT_DIR/protocol_times.csv"
exit "$FAILED"
