#!/usr/bin/env bash
set -euo pipefail

# Launch one two-way RF-PSI experiment: one-way RF-PSI followed by an encrypted
# reveal-back phase.  There is one OPA/PSI call; the sender learns the result
# from the reveal phase.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

N="${1:-1000}"
OVERLAP="${OVERLAP:-$((N / 5))}"
SEED="${SEED:-$(od -An -N8 -tu8 /dev/urandom | tr -d ' ')}"
init_run_dir "run_2pc_rf_psi2_${N}"
PSI2_ENV_LOCAL="$(common_env) CG_RF_THREADS=${CG_RF_THREADS_PSI2_LOCAL:-10}"
PSI2_ENV_REMOTE="$(common_env) CG_RF_THREADS=${CG_RF_THREADS_PSI2_REMOTE:-12}"
PSI_BITS="${CG_PSI_INPUT_BITS:-128}"
LOCAL_IO="/tmp/cg_rf_psi2_${N}"
REMOTE_IO="/tmp/cg_rf_psi2_${N}"
Q_ENV="$(common_env)"
Q_BOUND="$(ssh_local "cd '$LOCAL_ROOT/$BUILD_DIR' && export $Q_ENV; ./cg_print_q")"

echo "==> preparing rf_psi2_reveal_${N} inputs outside protocol timer"
echo "    PSI element bound: min(2^${PSI_BITS}, q), q=${Q_BOUND}"
ssh_local "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT'; python3 generate_sets.py '$N' '$N' '$OVERLAP' '$LOCAL_IO/set_A.txt' '$LOCAL_IO/set_B.txt' '$LOCAL_IO/true_intersection.txt' '$SEED' '$PSI_BITS' '$Q_BOUND'"
ssh_remote "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT'; python3 generate_sets.py '$N' '$N' '$OVERLAP' '$REMOTE_IO/set_A.txt' '$REMOTE_IO/set_B.txt' '$REMOTE_IO/true_intersection.txt' '$SEED' '$PSI_BITS' '$Q_BOUND'"

run_pair "rf_psi2_reveal_${N}" "local_first" \
    "cd '$LOCAL_ROOT'; CG_PRINT_RESULTS=1 CG_BUILD_DIR='$LOCAL_ROOT/$BUILD_DIR' $PSI2_ENV_LOCAL bash ./run_rf_cg_psi2_lan_b.sh '$REMOTE_IP' --file '$N' '$LOCAL_IO/set_B.txt'" \
    "cd '$REMOTE_ROOT'; CG_PRINT_RESULTS=1 CG_BUILD_DIR='$REMOTE_ROOT/$BUILD_DIR' $PSI2_ENV_REMOTE bash ./run_rf_cg_psi2_lan_a.sh '$LOCAL_IP' --file '$REMOTE_IO/set_A.txt' '$N'"
python3 "$SCRIPT_ROOT/refresh_psi_fast_protocol_times.py" "$OUT_DIR" || true
if [[ "${CG_SKIP_CHECKS:-0}" != "1" ]]; then
    check_psi2 "rf_psi2_reveal_${N}" "$LOCAL_IO" "$REMOTE_IO"
fi
python3 "$SCRIPT_ROOT/refresh_psi_fast_protocol_times.py" "$OUT_DIR" || true
finish_run
