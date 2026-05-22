#!/usr/bin/env bash
set -euo pipefail

# Launch one reverse-firewalled OPE experiment using RF-OLE underneath.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

N="${1:-1000}"
INPUT_BITS="${CG_INPUT_FILE_BITS:-128}"
init_run_dir "run_2pc_rf_ope_${N}"
ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"

LOCAL_IO="/tmp/cg_rf_ope_${N}"
REMOTE_IO="/tmp/cg_rf_ope_${N}"
echo "Preparing random ${INPUT_BITS}-bit RF-OPE input files outside timed protocol..."
ssh_local "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT'; python3 generate_protocol_inputs.py list 1 '$LOCAL_IO/alpha.txt' --bits '$INPUT_BITS'"
ssh_remote "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT'; python3 generate_protocol_inputs.py list '$((N + 1))' '$REMOTE_IO/coeffs.txt' --bits '$INPUT_BITS'"
run_pair "rf_ope_${N}" "local_first" \
    "cd '$LOCAL_ROOT' && CG_BUILD_DIR='$LOCAL_ROOT/$BUILD_DIR' $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO' CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time.csv' bash ./run_rf_cg_ope_lan_receiver.sh '$N' --alpha-file '$LOCAL_IO/alpha.txt'" \
    "cd '$REMOTE_ROOT' && CG_BUILD_DIR='$REMOTE_ROOT/$BUILD_DIR' $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time.csv' bash ./run_rf_cg_ope_lan_sender.sh '$LOCAL_IP' '$N' --input-file '$REMOTE_IO/coeffs.txt'"
check_ope "rf_ope_${N}" "$LOCAL_IO" "$REMOTE_IO" \
    rf_ope_sender_coeffs.txt rf_ope_receiver_output.txt
finish_run
