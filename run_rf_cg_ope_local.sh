#!/usr/bin/env bash
# Run RF-OPE on localhost: OPE sender/receiver over batched RF-OLE.
#
# Usage:
#   CG_RF_LANES=8 CG_RF_THREADS=12 bash run_rf_cg_ope_local.sh 1000

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
source "$ROOT/scripts/run_common.sh"
cg_run_init local
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build-portable}"
DEGREE="${1:-1000}"
cg_positive_size "$DEGREE" || exit 2
export CG_RF_EXPECTED_N="$DEGREE"
export CG_RF_LANES="${CG_RF_LANES:-4}"

if [[ ! -x "$BUILD_DIR/cg_rf_ope_receiver" || ! -x "$BUILD_DIR/cg_rf_ope_sender" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target cg_rf_ope_receiver cg_rf_ope_sender cg_rf_receiver_firewall_opa cg_rf_sender_firewall_opa -j
fi

LOG_DIR="$CG_RUN_LOG_DIR/ope_rf"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log \
      "$LOG_DIR"/rf_receiver.log "$LOG_DIR"/rf_sender.log

: # Ports are owned through the run lock; no global port cleanup.
sleep 1

echo "=== RF-OPE over batched RF-OLE: degree=$DEGREE, lanes=$CG_RF_LANES ==="

"$BUILD_DIR/cg_rf_ope_receiver" "$DEGREE" >"$LOG_DIR/receiver.log" 2>&1 &
PID_R=$!
sleep 2

"$BUILD_DIR/cg_rf_receiver_firewall_opa" >"$LOG_DIR/rf_receiver.log" 2>&1 &
PID_RRF=$!
sleep 2

"$BUILD_DIR/cg_rf_sender_firewall_opa" >"$LOG_DIR/rf_sender.log" 2>&1 &
PID_SRF=$!
sleep 2

"$BUILD_DIR/cg_rf_ope_sender" 127.0.0.1 "$DEGREE" >"$LOG_DIR/sender.log" 2>&1 &
PID_S=$!

cg_wait_all "$PID_R" "$PID_RRF" "$PID_SRF" "$PID_S"

echo "=== RF-OPE Logs ==="
cat "$LOG_DIR/receiver.log"
cat "$LOG_DIR/rf_receiver.log"
cat "$LOG_DIR/rf_sender.log"
cat "$LOG_DIR/sender.log"
