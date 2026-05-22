#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_ole3_receiver" || ! -x "$BUILD_DIR/cg_ole3_sender" ]]; then
    "$ROOT/build_local.sh"
fi

N="${1:-1000}"
PORT="${CG_OLE3_PORT_REC:-9123}"
export CG_RF_LANES="${CG_RF_LANES:-2}"
export CG_BENCH_IO_DIR="${CG_BENCH_IO_DIR:-$ROOT/logs/ole3_direct_io}"

LOG_DIR="$ROOT/logs/ole3_direct"
mkdir -p "$LOG_DIR" "$CG_BENCH_IO_DIR"
rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log \
      "$CG_BENCH_IO_DIR"/ole3_sender_inputs.txt \
      "$CG_BENCH_IO_DIR"/ole3_receiver_io.txt

fuser -k "$PORT/tcp" 2>/dev/null || true
sleep 1

cd "$BUILD_DIR"
echo "=== CG-AHE 3-round OLE: N=$N, lanes=$CG_RF_LANES, port=$PORT ==="
./cg_ole3_receiver "$N" "$PORT" >"$LOG_DIR/receiver.log" 2>&1 &
PID_R=$!
sleep 2
./cg_ole3_sender "$N" 127.0.0.1 "$PORT" >"$LOG_DIR/sender.log" 2>&1 &
PID_S=$!

wait "$PID_S"
wait "$PID_R"

./cg_check_batch_ole \
    "$CG_BENCH_IO_DIR/ole3_sender_inputs.txt" \
    "$CG_BENCH_IO_DIR/ole3_receiver_io.txt"

echo "=== Receiver Log ==="
cat "$LOG_DIR/receiver.log"
echo "=== Sender Log ==="
cat "$LOG_DIR/sender.log"
