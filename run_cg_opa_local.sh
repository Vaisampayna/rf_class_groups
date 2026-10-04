#!/usr/bin/env bash
# Run direct CG-AHE OLE-backed OPA on localhost.
#
# Usage:
#   CG_RF_LANES=8 CG_RF_THREADS=12 bash run_cg_opa_local.sh <d> "<pA degree 2d>" "<rA degree d>" "<pB degree d>"

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build-portable}"
D="${1:-1}"
N_PTS="$((2 * D + 1))"
PA="${2:-1 2 3}"
RA="${3:-1 1}"
PB="${4:--1 1}"
PORT="${CG_OPA_PORT_REC:-9105}"
export CG_RF_LANES="${CG_RF_LANES:-4}"

if [[ ! -x "$BUILD_DIR/cg_opa_receiver" || ! -x "$BUILD_DIR/cg_opa_sender" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target cg_opa_receiver cg_opa_sender -j
fi

LOG_DIR="$ROOT/logs/opa_direct"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log
fuser -k "$PORT/tcp" 2>/dev/null || true
sleep 1

echo "=== Direct OPA over batched OLE: d=$D, n_pts=$N_PTS, lanes=$CG_RF_LANES ==="
echo "p_A: $PA"
echo "r_A: $RA"
echo "p_B: $PB"

"$BUILD_DIR/cg_opa_receiver" "$N_PTS" "$D" $PB "$PORT" >"$LOG_DIR/receiver.log" 2>&1 &
PID_R=$!
sleep 1
"$BUILD_DIR/cg_opa_sender" 127.0.0.1 "$N_PTS" "$D" $PA -- $RA "$PORT" >"$LOG_DIR/sender.log" 2>&1 &
PID_S=$!

if ! wait "$PID_R"; then
    echo "ERROR: OPA receiver failed. Log follows:" >&2
    cat "$LOG_DIR/receiver.log" >&2 2>/dev/null || true
    exit 1
fi
if ! wait "$PID_S"; then
    echo "ERROR: OPA sender failed. Log follows:" >&2
    cat "$LOG_DIR/sender.log" >&2 2>/dev/null || true
    exit 1
fi

echo "=== Direct OPA Results ==="
cat "$LOG_DIR/receiver.log"
echo ""
echo "=== Timing Details ==="
grep -E "done in|OPTIME|process end-to-end" "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log || true
