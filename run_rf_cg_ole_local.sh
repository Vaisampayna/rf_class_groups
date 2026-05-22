#!/usr/bin/env bash
# run_rf_cg_ole_local.sh — Launch all 4 CG-AHE RF-OLE processes on localhost
#
# Usage: bash run_rf_cg_ole_local.sh <x> <a> <b>
# Example: bash run_rf_cg_ole_local.sh 5 3 7   => expects y = 3*5+7 = 22
#
# Startup order: Receiver → RF_R → RF_S → Sender

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_receiver" || ! -x "$BUILD_DIR/cg_rf_sender" ]]; then
    "$ROOT/build_local.sh"
fi
cd "$BUILD_DIR"

X=${1:-5}
A=${2:-3}
B=${3:-7}

LOG_DIR="$ROOT/logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/rf_receiver.log \
      "$LOG_DIR"/rf_sender.log "$LOG_DIR"/sender.log

echo "=== CG-AHE RF-OLE: x=$X, a=$A, b=$B (expected y=$((A*X+B))) ==="

# Start Receiver first (listens on :9003)
./cg_rf_receiver "$X" >"$LOG_DIR/receiver.log" 2>&1 &
PID_R=$!; sleep 3

# Start Receiver's RF (connects to :9003, listens on :9002)
./cg_rf_receiver_firewall >"$LOG_DIR/rf_receiver.log" 2>&1 &
PID_RFR=$!; sleep 3

# Start Sender's RF (connects to :9002, listens on :9001)
./cg_rf_sender_firewall >"$LOG_DIR/rf_sender.log" 2>&1 &
PID_RFS=$!; sleep 2

# Start Sender (connects to :9001)
./cg_rf_sender "$A" "$B" >"$LOG_DIR/sender.log" 2>&1 &
PID_S=$!

for item in \
    "$PID_R:$LOG_DIR/receiver.log:receiver" \
    "$PID_RFR:$LOG_DIR/rf_receiver.log:receiver firewall" \
    "$PID_RFS:$LOG_DIR/rf_sender.log:sender firewall" \
    "$PID_S:$LOG_DIR/sender.log:sender"; do
    IFS=: read -r pid file label <<<"$item"
    if ! wait "$pid"; then
        echo "ERROR: $label failed. Log follows:" >&2
        cat "$file" >&2 2>/dev/null || true
        exit 1
    fi
done

echo "=== Logs ==="
echo "--- receiver ---"; cat "$LOG_DIR/receiver.log"
echo "--- rf_receiver ---"; grep -v "^$" "$LOG_DIR/rf_receiver.log" || true
echo "--- rf_sender ---";  grep -v "^$" "$LOG_DIR/rf_sender.log"   || true
echo "--- sender ---";     cat "$LOG_DIR/sender.log"
