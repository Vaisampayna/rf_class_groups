#!/usr/bin/env bash
# run_rf_cg_psi_local.sh — CG-AHE RF-PSI on localhost
#
# Usage:
#   bash run_rf_cg_psi_local.sh [m_A] [m_B] [overlap] [seed]
#   bash run_rf_cg_psi_local.sh [--no-check] --files set_A.txt set_B.txt [true_intersection.txt]

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_psi_receiver" || ! -x "$BUILD_DIR/cg_rf_psi_sender" ]]; then
    "$ROOT/build_local.sh"
fi
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

LOG_DIR="$ROOT/logs"
INPUT_DIR="${CG_PSI_INPUT_DIR:-$ROOT/inputs/psi}"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/psi_receiver.log "$LOG_DIR"/psi_sender.log \
      "$LOG_DIR"/psi_rf_receiver.log "$LOG_DIR"/psi_rf_sender.log

if [[ "$ARG1" == "--files" ]]; then
    SET_A_FILE="${2:?missing set_A file}"
    SET_B_FILE="${3:?missing set_B file}"
    TRUE_FILE="${4:-}"
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
echo "    n_pts (OLE calls) = $((MA + MB + 1))"
echo "    transport lanes = $CG_RF_LANES"

# Stop processes bound to the demo ports before launching the pipeline.
for port in 9001 9002 9003; do
    fuser -k "$port/tcp" 2>/dev/null || true
done
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

for item in \
    "$PID_R:$LOG_DIR/psi_receiver.log:PSI receiver" \
    "$PID_RFR:$LOG_DIR/psi_rf_receiver.log:receiver firewall" \
    "$PID_RFS:$LOG_DIR/psi_rf_sender.log:sender firewall" \
    "$PID_S:$LOG_DIR/psi_sender.log:PSI sender"; do
    IFS=: read -r pid file label <<<"$item"
    if ! wait "$pid"; then
        echo "ERROR: $label failed. Log follows:" >&2
        cat "$file" >&2 2>/dev/null || true
        exit 1
    fi
done

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
