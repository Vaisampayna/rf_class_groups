#!/usr/bin/env bash
set -euo pipefail

# Launch one direct one-sided PSI experiment.  Both machines generate matching
# set files from the same seed before the timed receiver/sender commands start.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

N="${1:-1000}"
OVERLAP="${OVERLAP:-$((N / 5))}"
SEED="${SEED:-$(od -An -N8 -tu8 /dev/urandom | tr -d ' ')}"
init_run_dir "run_2pc_direct_psi_${N}"
ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"
PSI_BITS="${CG_PSI_INPUT_BITS:-128}"
Q_ENV="$(common_env)"
Q_BOUND="$(ssh_local "cd '$LOCAL_ROOT/$BUILD_DIR' && export $Q_ENV; ./cg_print_q")"

LOCAL_IO="/tmp/cg_direct_psi_${N}"
REMOTE_IO="/tmp/cg_direct_psi_${N}"
echo "==> preparing direct_psi_${N} inputs outside protocol timer"
echo "    PSI element bound: min(2^${PSI_BITS}, q), q=${Q_BOUND}"
run_pair "direct_psi_${N}" "local_first" \
    "rm -rf '$LOCAL_IO'; mkdir -p '$LOCAL_IO'; cd '$LOCAL_ROOT'; python3 generate_sets.py '$N' '$N' '$OVERLAP' '$LOCAL_IO/set_A.txt' '$LOCAL_IO/set_B.txt' '$LOCAL_IO/true_intersection.txt' '$SEED' '$PSI_BITS' '$Q_BOUND'; cd '$LOCAL_ROOT/$BUILD_DIR' && export $ENV_LOCAL CG_PROTOCOL_TIMING_FILE='$LOCAL_IO/protocol_time.csv'; ./cg_psi_receiver '$N' --input-file '$LOCAL_IO/set_B.txt' --output-file '$LOCAL_IO/intersection_out.txt' --timing-file '$LOCAL_IO/protocol_time.csv' --port 9103" \
    "rm -rf '$REMOTE_IO'; mkdir -p '$REMOTE_IO'; cd '$REMOTE_ROOT'; python3 generate_sets.py '$N' '$N' '$OVERLAP' '$REMOTE_IO/set_A.txt' '$REMOTE_IO/set_B.txt' '$REMOTE_IO/true_intersection.txt' '$SEED' '$PSI_BITS' '$Q_BOUND'; cd '$REMOTE_ROOT/$BUILD_DIR' && export $ENV_REMOTE CG_PROTOCOL_TIMING_FILE='$REMOTE_IO/protocol_time.csv'; ./cg_psi_sender '$LOCAL_IP' '$N' --input-file '$REMOTE_IO/set_A.txt' --port 9103"
check_psi "direct_psi_${N}" "$LOCAL_IO" "$REMOTE_IO"
finish_run
