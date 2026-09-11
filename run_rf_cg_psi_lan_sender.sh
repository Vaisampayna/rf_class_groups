#!/usr/bin/env bash
# run_rf_cg_psi_lan_sender.sh — Run PSI Sender and S-RF on System A (Sender Machine)
#
# START THIS AFTER run_rf_cg_psi_lan_receiver.sh is running on System B.
#
# USAGE — File input:
#   bash run_rf_cg_psi_lan_sender.sh <System_B_IP> --file set_A.txt <m_B>
#
# USAGE — Random benchmark mode:
#   bash run_rf_cg_psi_lan_sender.sh <System_B_IP> --random <m_A> <m_B> [seed]
#   e.g.: bash run_rf_cg_psi_lan_sender.sh 192.168.1.20 --random 1000 1000 42
#
# PORTS:
#   System B :9002 — Sender Firewall (S-RF) connects to this remotely
#   localhost :9001 — Sender Firewall listens for local PSI sender
#
# ENV VARS (can be overridden before calling this script):
#   CG_RF_LANES        — number of parallel TCP transport lanes (default: 12)
#   CG_RF_THREADS      — OpenMP threads for crypto workers (default: 12)
#   CG_RF_CHUNK_SIZE   — streaming chunk size (default: 128)
#   CG_Q_NBITS         — class group field size in bits (default: 128)
#   CG_CONNECT_RETRIES — max connection retries for TCP (default: 7200)

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"

# --- Build directory: prefer build-2pc (matches benchmark setup), fall back to build ---
BUILD_DIR="${CG_BUILD_DIR:-}"
if [[ -z "$BUILD_DIR" ]]; then
    if [[ -x "$ROOT/build-2pc/cg_rf_psi_sender" ]]; then
        BUILD_DIR="$ROOT/build-2pc"
    elif [[ -x "$ROOT/build/cg_rf_psi_sender" ]]; then
        BUILD_DIR="$ROOT/build"
    else
        echo "[sender] No pre-built binaries found. Running build_local.sh..."
        bash "$ROOT/build_local.sh"
        BUILD_DIR="$ROOT/build"
    fi
fi
echo "[sender] Using build dir: $BUILD_DIR"
cd "$BUILD_DIR"

# --- Environment: match the benchmark setup exactly ---
export CG_Q_NBITS="${CG_Q_NBITS:-128}"
export CG_K="${CG_K:-1}"
export CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}"
export CG_FIXED_Q="${CG_FIXED_Q:-170141183460469232709364739622490341377}"
export CG_RF_LANES="${CG_RF_LANES:-12}"
export CG_RF_CHUNK_SIZE="${CG_RF_CHUNK_SIZE:-128}"
export CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-7200}"
# Thread count: use CG_RF_THREADS if set, else CG_RF_THREADS_REMOTE, else 12
export CG_RF_THREADS="${CG_RF_THREADS:-${CG_RF_THREADS_REMOTE:-12}}"
export OMP_NUM_THREADS="$CG_RF_THREADS"

# --- Arguments ---
SYSTEM_B_IP="${1:?Usage: $0 <System_B_IP> [--random <m_A> <m_B> [seed]] OR [\"<S_A elements>\" <m_B>]}"
MODE="${2:-explicit}"

# --- Logging ---
LOG_DIR="$ROOT/logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/psi_sender.log "$LOG_DIR"/psi_rf_sender.log

echo "============================================="
echo " CG-AHE RF-PSI  —  System A (Sender Side)"
echo "============================================="
echo "  Build dir   : $BUILD_DIR"
echo "  System B IP : $SYSTEM_B_IP"
echo "  Lanes       : $CG_RF_LANES"
echo "  Threads     : $CG_RF_THREADS"
echo "  CG_Q_NBITS  : $CG_Q_NBITS"
echo "  CG_FIXED_Q  : $CG_FIXED_Q"
echo "  Chunk size  : $CG_RF_CHUNK_SIZE"
echo "  S-RF will connect to $SYSTEM_B_IP:9002"
echo "  S-RF listens on localhost:9001 for PSI sender"
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

# Sender Firewall: connects to System B at :9002, listens locally on :9001
./cg_rf_sender_firewall_opa "${SYSTEM_B_IP}" >"$LOG_DIR/psi_rf_sender.log" 2>&1 &
PID_RFS=$!
echo "[sender] Sender Firewall started (PID=$PID_RFS). Connecting to $SYSTEM_B_IP:9002..."
sleep 2

if [[ "$MODE" == "--random" ]]; then
    MA="${3:-1000}"
    MB="${4:-1000}"
    SEED="${5:-42}"
    echo "[sender] Mode: RANDOM  |S_A|=$MA  |S_B|=$MB  seed=$SEED"
    ./cg_rf_psi_sender "$MB" --random "$MA" "$SEED" \
        >"$LOG_DIR/psi_sender.log" 2>&1 &
elif [[ "$MODE" == "--file" ]]; then
    SET_A_FILE="${3:-$ROOT/set_A.txt}"
    MB="${4:-1000}"
    MA="$(wc -w <"$SET_A_FILE")"
    echo "[sender] Mode: FILE  |S_A|=$MA  |S_B|=$MB  set_A=$SET_A_FILE"
    ./cg_rf_psi_sender "$MB" --input-file "$SET_A_FILE" \
        >"$LOG_DIR/psi_sender.log" 2>&1 &
else
    # Explicit mode: arg2 = space-separated elements of S_A, arg3 = size of S_B
    SA="${2:-10 20 30 40 50}"
    MB="${3:-5}"
    MA=$(echo "$SA" | wc -w)
    echo "[sender] Mode: EXPLICIT  |S_A|=$MA  |S_B|=$MB"
    # shellcheck disable=SC2086
    ./cg_rf_psi_sender "$MB" $SA \
        >"$LOG_DIR/psi_sender.log" 2>&1 &
fi
PID_S=$!
echo "[sender] PSI sender started (PID=$PID_S)."
echo "[sender] Waiting for protocol to complete... (tail -f $LOG_DIR/psi_sender.log to monitor)"

wait_or_report "$PID_RFS" "sender firewall" "$LOG_DIR/psi_rf_sender.log"
wait_or_report "$PID_S"   "PSI sender"      "$LOG_DIR/psi_sender.log"

echo ""
echo "============================================="
echo " Timing Summary"
echo "============================================="
grep -E "n_pts|done in|online stream|precomp|end-to-end" \
    "$LOG_DIR/psi_sender.log" "$LOG_DIR/psi_rf_sender.log" || true
echo "[sender] All done. Logs saved to: $LOG_DIR/"
