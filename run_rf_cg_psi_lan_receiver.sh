#!/usr/bin/env bash
# run_rf_cg_psi_lan_receiver.sh — Run PSI Receiver and R-RF on System B (Receiver Machine)
#
# START THIS FIRST on the receiver-side machine.
#
# USAGE — File input:
#   bash run_rf_cg_psi_lan_receiver.sh --file set_B.txt <m_A> [intersection_out.txt] [protocol_time.csv]
#
# USAGE — Random benchmark mode:
#   bash run_rf_cg_psi_lan_receiver.sh --random <m_A> <m_B> [seed]
#   e.g.: bash run_rf_cg_psi_lan_receiver.sh --random 1000 1000 42
#
# PORTS USED (must be reachable from System A):
#   :9003  — PSI receiver listens (localhost only)
#   :9002  — Receiver Firewall (R-RF) listens — System A MUST reach this port!
#
# ENV VARS (can be overridden before calling this script):
#   CG_RF_LANES        — number of parallel TCP transport lanes (default: 12)
#   CG_RF_THREADS      — OpenMP threads for crypto workers (default: 10)
#   CG_RF_CHUNK_SIZE   — streaming chunk size (default: 128)
#   CG_Q_NBITS         — class group field size in bits (default: 128)
#   CG_CONNECT_RETRIES — max connection retries for TCP (default: 7200)

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"

# --- Build directory: prefer build-2pc (matches benchmark setup), fall back to build ---
BUILD_DIR="${CG_BUILD_DIR:-}"
if [[ -z "$BUILD_DIR" ]]; then
    if [[ -x "$ROOT/build-2pc/cg_rf_psi_receiver" ]]; then
        BUILD_DIR="$ROOT/build-2pc"
    elif [[ -x "$ROOT/build/cg_rf_psi_receiver" ]]; then
        BUILD_DIR="$ROOT/build"
    else
        echo "[receiver] No pre-built binaries found. Running build_local.sh..."
        bash "$ROOT/build_local.sh"
        BUILD_DIR="$ROOT/build"
    fi
fi
echo "[receiver] Using build dir: $BUILD_DIR"
cd "$BUILD_DIR"

# --- Environment: match the benchmark setup exactly ---
export CG_Q_NBITS="${CG_Q_NBITS:-128}"
export CG_K="${CG_K:-1}"
export CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}"
export CG_FIXED_Q="${CG_FIXED_Q:-170141183460469232709364739622490341377}"
export CG_RF_LANES="${CG_RF_LANES:-12}"
export CG_RF_CHUNK_SIZE="${CG_RF_CHUNK_SIZE:-128}"
export CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-7200}"
# Thread count: use CG_RF_THREADS if set, else CG_RF_THREADS_LOCAL, else 10
export CG_RF_THREADS="${CG_RF_THREADS:-${CG_RF_THREADS_LOCAL:-10}}"
export OMP_NUM_THREADS="$CG_RF_THREADS"

# --- Logging ---
LOG_DIR="$ROOT/logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/psi_receiver.log "$LOG_DIR"/psi_rf_receiver.log

echo "============================================="
echo " CG-AHE RF-PSI  —  System B (Receiver Side)"
echo "============================================="
echo "  Build dir   : $BUILD_DIR"
echo "  Lanes       : $CG_RF_LANES"
echo "  Threads     : $CG_RF_THREADS"
echo "  CG_Q_NBITS  : $CG_Q_NBITS"
echo "  CG_FIXED_Q  : $CG_FIXED_Q"
echo "  Chunk size  : $CG_RF_CHUNK_SIZE"
echo "  Port 9003   : PSI receiver (localhost)"
echo "  Port 9002   : Receiver Firewall — System A must reach THIS port"
echo "============================================="

wait_or_report() {
    local pid="$1" label="$2" file="$3"
    set +e; wait "$pid"; local status=$?; set -e
    if (( status != 0 )); then
        echo "ERROR: $label failed (exit $status). Tail of log:" >&2
        tail -n 40 "$file" >&2 2>/dev/null || true
        exit "$status"
    fi
}

MODE="${1:---file}"

if [[ "$MODE" == "--random" ]]; then
    MA="${2:-1000}"
    MB="${3:-1000}"
    SEED="${4:-42}"
    echo "[receiver] Mode: RANDOM  |S_A|=$MA  |S_B|=$MB  seed=$SEED"
    ./cg_rf_psi_receiver "$MA" --random "$MB" "$SEED" \
        >"$LOG_DIR/psi_receiver.log" 2>&1 &
elif [[ "$MODE" == "--file" ]]; then
    SET_B_FILE="${2:-$ROOT/set_B.txt}"
    MA="${3:-1000}"
    OUT_FILE="${4:-$ROOT/intersection_out.txt}"
    TIMING_FILE="${5:-}"
    MB="$(wc -w <"$SET_B_FILE")"
    echo "[receiver] Mode: FILE  |S_A|=$MA  |S_B|=$MB  set_B=$SET_B_FILE"
    if [[ -n "$TIMING_FILE" ]]; then
        ./cg_rf_psi_receiver "$MA" --input-file "$SET_B_FILE" \
            --output-file "$OUT_FILE" --timing-file "$TIMING_FILE" \
            >"$LOG_DIR/psi_receiver.log" 2>&1 &
    else
        ./cg_rf_psi_receiver "$MA" --input-file "$SET_B_FILE" \
            --output-file "$OUT_FILE" \
            >"$LOG_DIR/psi_receiver.log" 2>&1 &
    fi
else
    # Explicit mode: arg1 = space-separated elements of S_B, arg2 = size of S_A
    SB="${1:-30 40 50 60 70}"
    MA="${2:-5}"
    MB=$(echo "$SB" | wc -w)
    echo "[receiver] Mode: EXPLICIT  |S_A|=$MA  |S_B|=$MB"
    # shellcheck disable=SC2086
    ./cg_rf_psi_receiver "$MA" $SB \
        >"$LOG_DIR/psi_receiver.log" 2>&1 &
fi
PID_R=$!

# Receiver Firewall: connects to localhost:9003, listens on :9002 for System A.
# Start it immediately after the receiver.  The firewall uses retrying connects,
# so a fixed sleep here would be counted inside the receiver's protocol clock
# while it is blocked in accept_rf_lanes().
./cg_rf_receiver_firewall_opa >"$LOG_DIR/psi_rf_receiver.log" 2>&1 &
PID_RFR=$!
echo "[receiver] PSI receiver started (PID=$PID_R)."
echo "[receiver] Receiver Firewall started immediately (PID=$PID_RFR). Listening on :9002 for System A."
echo "[receiver] Waiting for protocol to complete... (tail -f $LOG_DIR/psi_receiver.log to monitor)"

wait_or_report "$PID_R"   "PSI receiver"      "$LOG_DIR/psi_receiver.log"
wait_or_report "$PID_RFR" "receiver firewall" "$LOG_DIR/psi_rf_receiver.log"

echo ""
echo "============================================="
echo " PSI Results"
echo "============================================="
cat "$LOG_DIR/psi_receiver.log"

echo ""
echo "============================================="
echo " Timing Summary"
echo "============================================="
grep -E "n_pts|done in|online stream|precomp|total intersection|intersection size|end-to-end" \
    "$LOG_DIR/psi_receiver.log" "$LOG_DIR/psi_rf_receiver.log" || true
