#!/usr/bin/env bash
set -euo pipefail

# Launch one reverse-firewalled batched OLE experiment.  Receiver and sender
# inputs are prepared first; endpoint clocks exclude file generation.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

N="${1:-1000}"
INPUT_BITS="${CG_INPUT_FILE_BITS:-128}"
init_run_dir "run_2pc_rf_ole_${N}"
ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"

LOCAL_IO="/tmp/cg_rf_batched_ole_${N}"
REMOTE_IO="/tmp/cg_rf_batched_ole_${N}"
echo "Preparing random ${INPUT_BITS}-bit RF-OLE input files outside timed protocol..."
ssh_local "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT'; python3 generate_protocol_inputs.py list '$N' '$LOCAL_IO/x.txt' --bits '$INPUT_BITS'"
ssh_remote "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT'; python3 generate_protocol_inputs.py ole-sender '$N' '$REMOTE_IO/sender_inputs.txt' --bits '$INPUT_BITS'"
run_pair "rf_batched_ole_${N}" "local_first" \
    "cd '$LOCAL_ROOT/$BUILD_DIR'; export $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO'; CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time.csv' ./cg_rf_batch_ole_receiver '$N' --input-file '$LOCAL_IO/x.txt' & ./cg_rf_receiver_firewall_opa; wait" \
    "cd '$REMOTE_ROOT/$BUILD_DIR'; export $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time.csv'; ./cg_rf_sender_firewall_opa '$LOCAL_IP' & sleep 1; ./cg_rf_batch_ole_sender '$N' --input-file '$REMOTE_IO/sender_inputs.txt'; wait"
check_batch_ole "rf_batched_ole_${N}" "$LOCAL_IO" "$REMOTE_IO" \
    rf_ole_sender_inputs.txt rf_ole_receiver_io.txt
finish_run
