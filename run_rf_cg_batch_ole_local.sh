#!/usr/bin/env bash
# run_rf_cg_batch_ole_local.sh — Run Batched RF-OLE processes on localhost
#
# Usage: bash run_rf_cg_batch_ole_local.sh <N>
# Example: bash run_rf_cg_batch_ole_local.sh 1000

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_batch_ole_receiver" || ! -x "$BUILD_DIR/cg_rf_batch_ole_sender" ]]; then
    "$ROOT/build_local.sh"
fi
cd "$BUILD_DIR"

N=${1:-1000}
export CG_RF_LANES="${CG_RF_LANES:-2}"

LOG_DIR="$ROOT/logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/batch_ole_receiver.log "$LOG_DIR"/batch_ole_sender.log \
      "$LOG_DIR"/batch_ole_rf_receiver.log "$LOG_DIR"/batch_ole_rf_sender.log

echo "=== CG-AHE Batched RF-OLE: N=$N, lanes=$CG_RF_LANES ==="

for port in 9001 9002 9003 9004; do
    fuser -k "$port/tcp" 2>/dev/null || true
done
sleep 1

# Batched Receiver (listens :9003)
./cg_rf_batch_ole_receiver "$N" >"$LOG_DIR/batch_ole_receiver.log" 2>&1 &
PID_R=$!; sleep 4

# RF_R looping (connects :9003, listens :9002)
./cg_rf_receiver_firewall_opa >"$LOG_DIR/batch_ole_rf_receiver.log" 2>&1 &
PID_RFR=$!; sleep 3

# RF_S looping (connects :9002, listens :9001)
./cg_rf_sender_firewall_opa >"$LOG_DIR/batch_ole_rf_sender.log" 2>&1 &
PID_RFS=$!; sleep 2

# Batched Sender (connects :9001)
./cg_rf_batch_ole_sender "$N" >"$LOG_DIR/batch_ole_sender.log" 2>&1 &
PID_S=$!

for item in \
    "$PID_R:$LOG_DIR/batch_ole_receiver.log:batch OLE receiver" \
    "$PID_RFR:$LOG_DIR/batch_ole_rf_receiver.log:receiver firewall" \
    "$PID_RFS:$LOG_DIR/batch_ole_rf_sender.log:sender firewall" \
    "$PID_S:$LOG_DIR/batch_ole_sender.log:batch OLE sender"; do
    IFS=: read -r pid file label <<<"$item"
    if ! wait "$pid"; then
        echo "ERROR: $label failed. Log follows:" >&2
        cat "$file" >&2 2>/dev/null || true
        exit 1
    fi
done

echo "=== Logs ==="
cat "$LOG_DIR/batch_ole_receiver.log"
cat "$LOG_DIR/batch_ole_sender.log"
