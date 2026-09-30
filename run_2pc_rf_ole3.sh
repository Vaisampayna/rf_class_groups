#!/usr/bin/env bash
set -euo pipefail

# Launch one reverse-firewalled three-round OLE experiment and verify the
# resulting OLE relation against the firewalls' sanitized sender tuple.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

N="${1:-1000}"
init_run_dir "run_2pc_rf_ole3_${N}"
ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"
LOCAL_IO="/tmp/cg_rf_ole3_${N}"
REMOTE_IO="/tmp/cg_rf_ole3_${N}"
ssh_local "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'"
ssh_remote "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'"

run_pair "rf_ole3_${N}" "local_first" \
    "cd '$LOCAL_ROOT/$BUILD_DIR'; export $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO'; CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time.csv' ./cg_rf_ole3_receiver '$N' & ./cg_rf_receiver_firewall_ole3; wait" \
    "cd '$REMOTE_ROOT/$BUILD_DIR'; export $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time.csv'; ./cg_rf_sender_firewall_ole3 '$LOCAL_IP' & sleep 1; ./cg_rf_ole3_sender '$N'; wait"
check_batch_ole "rf_ole3_${N}" "$LOCAL_IO" "$REMOTE_IO" \
    rf_ole3_sender_inputs.txt rf_ole3_receiver_io.txt \
    rf_ole3_sender_firewall_blinds.txt rf_ole3_receiver_firewall_blinds.txt
finish_run
