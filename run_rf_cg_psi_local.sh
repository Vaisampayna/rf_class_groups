#!/usr/bin/env bash
# run_rf_cg_psi_local.sh — CG-AHE RF-PSI on localhost
#
# Usage:
#   bash run_rf_cg_psi_local.sh [m_A] [m_B] [overlap] [seed]
#   bash run_rf_cg_psi_local.sh [--no-check] --files set_A.txt set_B.txt [true_intersection.txt]

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
source "$ROOT/scripts/run_common.sh"
cg_run_init local
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_psi_receiver" || ! -x "$BUILD_DIR/cg_rf_psi_sender" ]]; then
    "$ROOT/build_local.sh"
fi
CALLER_DIR="$PWD"
cd "$BUILD_DIR"

SKIP_CHECK="${CG_SKIP_CHECKS:-${CG_SKIP_CHECK:-0}}"
if [[ "${1:-}" == "--no-check" ]]; then
    SKIP_CHECK=1
    shift
fi
ARG1="${1:-100}"
ARG2="${2:-100}"
SEED="${4:-42}"
export CG_RF_LANES="${CG_RF_LANES:-2}"
export CG_Q_NBITS="${CG_Q_NBITS:-128}"
export CG_K="${CG_K:-1}"
export CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}"
export CG_FIXED_Q="${CG_FIXED_Q:-170141183460469232709364739622490341377}"

LOG_DIR="$CG_RUN_LOG_DIR"
INPUT_DIR="${CG_PSI_INPUT_DIR:-$ROOT/inputs/psi}"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/psi_receiver.log "$LOG_DIR"/psi_sender.log \
      "$LOG_DIR"/psi_rf_receiver.log "$LOG_DIR"/psi_rf_sender.log

if [[ "$ARG1" == "--files" ]]; then
    SET_A_FILE="${2:?missing set_A file}"
[[ -z "${SET_A_FILE:-}" || "$SET_A_FILE" == /* ]] || SET_A_FILE="$CALLER_DIR/$SET_A_FILE"
    SET_B_FILE="${3:?missing set_B file}"
[[ -z "${SET_B_FILE:-}" || "$SET_B_FILE" == /* ]] || SET_B_FILE="$CALLER_DIR/$SET_B_FILE"
    TRUE_FILE="${4:-}"
[[ -z "${TRUE_FILE:-}" || "$TRUE_FILE" == /* ]] || TRUE_FILE="$CALLER_DIR/$TRUE_FILE"
else
    MA="$ARG1"
    MB="$ARG2"
    OVERLAP="${3:-$(( MA < MB ? MA / 5 : MB / 5 ))}"
    mkdir -p "$INPUT_DIR"
    SET_A_FILE="$INPUT_DIR/set_A.txt"
    SET_B_FILE="$INPUT_DIR/set_B.txt"
    TRUE_FILE="$INPUT_DIR/true_intersection.txt"
    python3 "$ROOT/generate_sets.py" "$MA" "$MB" "$OVERLAP" \
        "$SET_A_FILE" "$SET_B_FILE" "$TRUE_FILE" "$SEED" 128 "$CG_FIXED_Q"
fi
MA="$(wc -w <"$SET_A_FILE")"
MB="$(wc -w <"$SET_B_FILE")"
OUT_FILE="$LOG_DIR/intersection_out.txt"
rm -f "$OUT_FILE"

echo "=== CG-AHE RF-PSI (file inputs, 128-bit plaintexts) ==="
echo "    S_A file = $SET_A_FILE (m_A=$MA)"
echo "    S_B file = $SET_B_FILE (m_B=$MB)"
if [[ -n "$TRUE_FILE" ]]; then
    echo "    true file = $TRUE_FILE"
else
    echo "    true file = not supplied; checker disabled"
fi
echo "    receiver output = $OUT_FILE"
echo "    plaintext q = $CG_FIXED_Q"
RAW_N=$((MA + MB + 1))
export CG_RF_EXPECTED_N="$RAW_N"
if [[ "${CG_USE_NTT_POLY:-1}" != 0 && "$CG_FIXED_Q" == 170141183460469232709364739622490341377 ]]; then
    CG_RF_EXPECTED_N=1
    while (( CG_RF_EXPECTED_N < RAW_N )); do CG_RF_EXPECTED_N=$((CG_RF_EXPECTED_N * 2)); done
fi
echo "    n_pts (OLE calls) = $CG_RF_EXPECTED_N"
echo "    transport lanes = $CG_RF_LANES"

: # Ports are owned through the run lock; no global port cleanup.
sleep 1

# ── Launch pipeline (Receiver → R-RF → S-RF → Sender) ──────────────────────

./cg_rf_psi_receiver "$MA" --input-file "$SET_B_FILE" --output-file "$OUT_FILE" \
    > "$LOG_DIR/psi_receiver.log" 2>&1 &
PID_R=$!; sleep 5

./cg_rf_receiver_firewall_opa > "$LOG_DIR/psi_rf_receiver.log" 2>&1 &
PID_RFR=$!; sleep 3

./cg_rf_sender_firewall_opa   > "$LOG_DIR/psi_rf_sender.log"   2>&1 &
PID_RFS=$!; sleep 2

./cg_rf_psi_sender "$MB" --input-file "$SET_A_FILE" \
    > "$LOG_DIR/psi_sender.log" 2>&1 &
PID_S=$!

echo ""
echo "Processes started. Waiting for completion..."
echo "  Receiver PID=$PID_R, R-RF PID=$PID_RFR, S-RF PID=$PID_RFS, Sender PID=$PID_S"
echo ""

cg_wait_all "$PID_R" "$PID_RFR" "$PID_RFS" "$PID_S"

echo "=== PSI Results ==="
cat "$LOG_DIR/psi_receiver.log"
if [[ "$SKIP_CHECK" == "1" ]]; then
    echo "Correctness checker disabled."
elif [[ -n "$TRUE_FILE" ]]; then
    python3 "$ROOT/check_correctness.py" "$TRUE_FILE" "$OUT_FILE"
else
    echo "No true intersection file supplied; skipping external correctness check."
fi
echo ""
echo "=== Timing Details ==="
grep -E "precomp|online stream|done in|n_pts" \
    "$LOG_DIR"/psi_receiver.log "$LOG_DIR"/psi_rf_receiver.log \
    "$LOG_DIR"/psi_rf_sender.log "$LOG_DIR"/psi_sender.log || true
