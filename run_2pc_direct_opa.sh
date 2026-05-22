#!/usr/bin/env bash
set -euo pipefail

# Launch one direct OPA-over-OLE experiment and verify all public-point outputs.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

N="${1:-1000}"
INPUT_BITS="${CG_INPUT_FILE_BITS:-128}"
init_run_dir "run_2pc_direct_opa_${N}"
ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"
LOCAL_IO="/tmp/cg_direct_opa_${N}"
REMOTE_IO="/tmp/cg_direct_opa_${N}"
echo "Preparing random ${INPUT_BITS}-bit OPA input files outside timed protocol..."
ssh_local "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT'; python3 generate_protocol_inputs.py list '$((N + 1))' '$LOCAL_IO/pB.txt' --bits '$INPUT_BITS'"
ssh_remote "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT'; python3 generate_protocol_inputs.py list '$((N + 1))' '$REMOTE_IO/pA.txt' --bits '$INPUT_BITS'; python3 generate_protocol_inputs.py list '$((N + 1))' '$REMOTE_IO/rA.txt' --bits '$INPUT_BITS'"

run_pair "direct_opa_${N}" "local_first" \
    "cd '$LOCAL_ROOT/$BUILD_DIR' && export $ENV_LOCAL CG_BENCH_IO_DIR='$LOCAL_IO' CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time.csv'; ./cg_opa_receiver '$N' '$N' --input-file '$LOCAL_IO/pB.txt' 9103" \
    "cd '$REMOTE_ROOT/$BUILD_DIR' && export $ENV_REMOTE CG_BENCH_IO_DIR='$REMOTE_IO' CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time.csv'; ./cg_opa_sender '$LOCAL_IP' '$N' '$N' --input-file '$REMOTE_IO/pA.txt' -- --input-file '$REMOTE_IO/rA.txt' 9103"
check_opa "direct_opa_${N}" "$LOCAL_IO" "$REMOTE_IO" \
    direct_opa_sender_coeffs.txt direct_opa_receiver_output.txt
finish_run
