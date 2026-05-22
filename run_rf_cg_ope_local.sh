#!/usr/bin/env bash
# Run RF-OPE on localhost: OPE sender/receiver over batched RF-OLE.
#
# Usage:
#   CG_RF_LANES=8 CG_RF_THREADS=12 bash run_rf_cg_ope_local.sh 1000

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build-portable}"
DEGREE="${1:-1000}"
export CG_RF_LANES="${CG_RF_LANES:-4}"

if [[ ! -x "$BUILD_DIR/cg_rf_ope_receiver" || ! -x "$BUILD_DIR/cg_rf_ope_sender" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target cg_rf_ope_receiver cg_rf_ope_sender cg_rf_receiver_firewall_opa cg_rf_sender_firewall_opa -j
fi

LOG_DIR="$ROOT/logs/ope_rf"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log \
      "$LOG_DIR"/rf_receiver.log "$LOG_DIR"/rf_sender.log

for port in "${CG_PORT_RFS:-9001}" "${CG_PORT_RFR:-9002}" "${CG_PORT_REC:-9003}" "${CG_OPE_PORT_VERIFY:-9104}"; do
    fuser -k "$port/tcp" 2>/dev/null || true
done
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

for item in \
    "$PID_R:$LOG_DIR/receiver.log:RF-OPE receiver" \
    "$PID_RRF:$LOG_DIR/rf_receiver.log:receiver firewall" \
    "$PID_SRF:$LOG_DIR/rf_sender.log:sender firewall" \
    "$PID_S:$LOG_DIR/sender.log:RF-OPE sender"; do
    IFS=: read -r pid file label <<<"$item"
    if ! wait "$pid"; then
        echo "ERROR: $label failed. Log follows:" >&2
        cat "$file" >&2 2>/dev/null || true
        exit 1
    fi
done

echo "=== RF-OPE Logs ==="
cat "$LOG_DIR/receiver.log"
cat "$LOG_DIR/rf_receiver.log"
cat "$LOG_DIR/rf_sender.log"
cat "$LOG_DIR/sender.log"
