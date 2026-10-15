#!/usr/bin/env bash
# Run direct CG-AHE OLE-backed OPA on localhost.
#
# Usage:
#   CG_RF_LANES=8 CG_RF_THREADS=12 bash run_cg_opa_local.sh <d> "<pA degree 2d>" "<pB degree d>"

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
source "$ROOT/scripts/run_common.sh"
cg_run_init local
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build-portable}"
D="${1:-1}"
N_PTS="$((2 * D + 1))"
PA="${2:-1 2 3}"
PB="${3:--1 1}"
PORT="${CG_OPA_PORT_REC:-9105}"
export CG_RF_LANES="${CG_RF_LANES:-4}"

if [[ ! -x "$BUILD_DIR/cg_opa_receiver" || ! -x "$BUILD_DIR/cg_opa_sender" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target cg_opa_receiver cg_opa_sender -j
fi

LOG_DIR="$CG_RUN_LOG_DIR/opa_direct"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log
sleep 1

echo "=== Direct OPA over batched OLE: d=$D, n_pts=$N_PTS, lanes=$CG_RF_LANES ==="
echo "p_A: $PA"
echo "r_A: sampled internally by sender"
echo "p_B: $PB"

"$BUILD_DIR/cg_opa_receiver" "$N_PTS" "$D" $PB "$PORT" >"$LOG_DIR/receiver.log" 2>&1 &
PID_R=$!
sleep 1
"$BUILD_DIR/cg_opa_sender" 127.0.0.1 "$N_PTS" "$D" $PA "$PORT" >"$LOG_DIR/sender.log" 2>&1 &
PID_S=$!

cg_wait_all "$PID_R" "$PID_S"

echo "=== Direct OPA Results ==="
cat "$LOG_DIR/receiver.log"
echo ""
echo "=== Timing Details ==="
grep -E "done in|OPTIME|process end-to-end" "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log || true
