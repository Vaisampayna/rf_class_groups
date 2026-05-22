#!/usr/bin/env bash
set -euo pipefail

# Launch one reverse-firewalled one-sided PSI experiment over RF-OPA/RF-OLE.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

N="${1:-1000}"
OVERLAP="${OVERLAP:-$((N / 5))}"
SEED="${SEED:-$(od -An -N8 -tu8 /dev/urandom | tr -d ' ')}"
init_run_dir "run_2pc_rf_psi_${N}"
ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"
PSI_BITS="${CG_PSI_INPUT_BITS:-128}"

LOCAL_IO="/tmp/cg_rf_psi_${N}"
REMOTE_IO="/tmp/cg_rf_psi_${N}"
run_pair "rf_psi_${N}" "local_first" \
    "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT'; python3 generate_sets.py '$N' '$N' '$OVERLAP' '$LOCAL_IO/set_A.txt' '$LOCAL_IO/set_B.txt' '$LOCAL_IO/true_intersection.txt' '$SEED' '$PSI_BITS'; CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time.csv' CG_BUILD_DIR='$LOCAL_ROOT/$BUILD_DIR' $ENV_LOCAL bash ./run_rf_cg_psi_lan_receiver.sh --file '$LOCAL_IO/set_B.txt' '$N' '$LOCAL_IO/intersection_out.txt' '$LOCAL_IO/protocol_time.csv'" \
    "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT'; python3 generate_sets.py '$N' '$N' '$OVERLAP' '$REMOTE_IO/set_A.txt' '$REMOTE_IO/set_B.txt' '$REMOTE_IO/true_intersection.txt' '$SEED' '$PSI_BITS'; CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time.csv' CG_BUILD_DIR='$REMOTE_ROOT/$BUILD_DIR' $ENV_REMOTE bash ./run_rf_cg_psi_lan_sender.sh '$LOCAL_IP' --file '$REMOTE_IO/set_A.txt' '$N'"
check_psi "rf_psi_${N}" "$LOCAL_IO" "$REMOTE_IO"
finish_run
