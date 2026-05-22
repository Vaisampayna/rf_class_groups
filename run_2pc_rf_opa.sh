#!/usr/bin/env bash
set -euo pipefail

# Launch one reverse-firewalled OPA experiment through the two RF-OLE firewalls.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

N="${1:-1000}"
INPUT_BITS="${CG_INPUT_FILE_BITS:-128}"
init_run_dir "run_2pc_rf_opa_${N}"
ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"
LOCAL_IO="/tmp/cg_rf_opa_${N}"
REMOTE_IO="/tmp/cg_rf_opa_${N}"
echo "Preparing random ${INPUT_BITS}-bit RF-OPA input files outside timed protocol..."
ssh_local "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT'; python3 generate_protocol_inputs.py list '$((N + 1))' '$LOCAL_IO/pB.txt' --bits '$INPUT_BITS'"
ssh_remote "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT'; python3 generate_protocol_inputs.py list '$((N + 1))' '$REMOTE_IO/pA.txt' --bits '$INPUT_BITS'; python3 generate_protocol_inputs.py list '$((N + 1))' '$REMOTE_IO/rA.txt' --bits '$INPUT_BITS'"

run_pair "rf_opa_${N}" "local_first" \
    "cd '$LOCAL_ROOT/$BUILD_DIR'; export $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO'; CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time.csv' ./cg_rf_opa_receiver '$N' '$N' --input-file '$LOCAL_IO/pB.txt' & ./cg_rf_receiver_firewall_opa; wait" \
    "cd '$REMOTE_ROOT/$BUILD_DIR'; export $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time.csv'; ./cg_rf_sender_firewall_opa '$LOCAL_IP' & sleep 1; ./cg_rf_opa_sender '$N' '$N' --input-file '$REMOTE_IO/pA.txt' -- --input-file '$REMOTE_IO/rA.txt'; wait"
check_opa "rf_opa_${N}" "$LOCAL_IO" "$REMOTE_IO" \
    rf_opa_sender_coeffs.txt rf_opa_receiver_output.txt
finish_run
