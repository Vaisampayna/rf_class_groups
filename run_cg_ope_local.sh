#!/usr/bin/env bash
# Run direct CG-AHE OLE-backed OPE on localhost.
#
# Usage:
#   CG_RF_LANES=8 CG_RF_THREADS=12 bash run_cg_ope_local.sh 1000

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build-portable}"
DEGREE="${1:-1000}"
export CG_RF_LANES="${CG_RF_LANES:-4}"

if [[ ! -x "$BUILD_DIR/cg_ope_receiver" || ! -x "$BUILD_DIR/cg_ope_sender" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target cg_ope_receiver cg_ope_sender -j
fi

LOG_DIR="$ROOT/logs/ope_direct"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log

for port in "${CG_OPE_PORT_REC:-9103}" "${CG_OPE_PORT_VERIFY:-9104}"; do
    fuser -k "$port/tcp" 2>/dev/null || true
done
sleep 1

echo "=== Direct OPE over batched OLE: degree=$DEGREE, lanes=$CG_RF_LANES ==="

"$BUILD_DIR/cg_ope_receiver" "$DEGREE" >"$LOG_DIR/receiver.log" 2>&1 &
PID_R=$!
sleep 1

"$BUILD_DIR/cg_ope_sender" 127.0.0.1 "$DEGREE" >"$LOG_DIR/sender.log" 2>&1 &
PID_S=$!

if ! wait "$PID_R"; then
    echo "ERROR: OPE receiver failed. Log follows:" >&2
    cat "$LOG_DIR/receiver.log" >&2 2>/dev/null || true
    exit 1
fi
if ! wait "$PID_S"; then
    echo "ERROR: OPE sender failed. Log follows:" >&2
    cat "$LOG_DIR/sender.log" >&2 2>/dev/null || true
    exit 1
fi

echo "=== Direct OPE Logs ==="
cat "$LOG_DIR/receiver.log"
cat "$LOG_DIR/sender.log"
