#!/usr/bin/env bash
set -euo pipefail

# Launch one direct two-way PSI experiment. Party B first learns the
# intersection using direct PSI, then sends that intersection directly back to
# Party A over a clear reveal socket.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

N="${1:-1000}"
OVERLAP="${OVERLAP:-$((N / 5))}"
SEED="${SEED:-$(od -An -N8 -tu8 /dev/urandom | tr -d ' ')}"
init_run_dir "run_2pc_direct_psi2_${N}"
ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"
PSI_BITS="${CG_PSI_INPUT_BITS:-128}"
PORT="${CG_DIRECT_PSI2_PORT:-9103}"
REVEAL_PORT="${CG_DIRECT_PSI2_REVEAL_PORT:-9143}"

LOCAL_IO="/tmp/cg_direct_psi2_${N}"
REMOTE_IO="/tmp/cg_direct_psi2_${N}"
run_pair "direct_psi2_${N}" "local_first" \
    "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT'; python3 generate_sets.py '$N' '$N' '$OVERLAP' '$LOCAL_IO/set_A.txt' '$LOCAL_IO/set_B.txt' '$LOCAL_IO/true_intersection.txt' '$SEED' '$PSI_BITS'; cd '$LOCAL_ROOT/$BUILD_DIR' && export $ENV_LOCAL CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time_B.csv' CG_PSI2_DIRECT_REVEAL_PORT='$REVEAL_PORT'; ./cg_psi2_receiver '$REMOTE_IP' '$N' --input-file '$LOCAL_IO/set_B.txt' --output-file '$LOCAL_IO/intersection_out.txt' --timing-file '$LOCAL_IO/protocol_time_B.csv' --port '$PORT' --reveal-port '$REVEAL_PORT'" \
    "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT'; python3 generate_sets.py '$N' '$N' '$OVERLAP' '$REMOTE_IO/set_A.txt' '$REMOTE_IO/set_B.txt' '$REMOTE_IO/true_intersection.txt' '$SEED' '$PSI_BITS'; cd '$REMOTE_ROOT/$BUILD_DIR' && export $ENV_REMOTE CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time_A.csv' CG_PSI2_DIRECT_REVEAL_PORT='$REVEAL_PORT'; ./cg_psi2_sender '$LOCAL_IP' '$N' --input-file '$REMOTE_IO/set_A.txt' --port '$PORT' --reveal-port '$REVEAL_PORT' --output-file '$REMOTE_IO/sender_intersection_out.txt'"

if [[ "${CG_SKIP_CHECKS:-0}" != "1" ]]; then
    check_direct_psi2 "direct_psi2_${N}" "$LOCAL_IO" "$REMOTE_IO"
else
    mkdir -p "$IO_DIR/direct_psi2_${N}"
    fetch_protocol_time "$LOCAL_IO" "$IO_DIR/direct_psi2_${N}" protocol_time_B.csv "direct_psi2_${N}"
    fetch_remote_protocol_time "$REMOTE_IO" "$IO_DIR/direct_psi2_${N}" protocol_time_A.csv "direct_psi2_${N}"
fi
finish_run
