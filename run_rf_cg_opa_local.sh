#!/usr/bin/env bash
# run_rf_cg_opa_local.sh — Launch 4 CG-AHE RF-OPA processes on localhost
#
# Usage: bash run_rf_cg_opa_local.sh <d> "<pA coeffs degree 2d>" "<rA coeffs degree d>" "<pB coeffs degree d>"
# Example (d=1):
#   pA(X) = 3X^2 + 2X + 1            -> coeffs: 1 2 3
#   rA(X) = X + 1                    -> coeffs: 1 1
#   pB(X) = X - 1                    -> coeffs: -1 1
#   p_inter at alpha points = pA + rA*pB

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_opa_receiver" || ! -x "$BUILD_DIR/cg_rf_opa_sender" ]]; then
    "$ROOT/build_local.sh"
fi
cd "$BUILD_DIR"

D=${1:-1}
N_PTS=$((2 * D + 1))
PA="${2:-1 2 3}"
RA="${3:-1 1}"
PB="${4:--1 1}"
export CG_RF_LANES="${CG_RF_LANES:-2}"

LOG_DIR="$ROOT/logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/opa_receiver.log "$LOG_DIR"/opa_sender.log \
      "$LOG_DIR"/opa_rf_receiver.log "$LOG_DIR"/opa_rf_sender.log

echo "=== CG-AHE RF-OPA: d=$D, n_pts=$N_PTS, lanes=$CG_RF_LANES ==="
echo "    p_A coeffs (const..degree): $PA"
echo "    r_A coeffs: $RA"
echo "    p_B coeffs: $PB"

for port in 9001 9002 9003; do
    fuser -k "$port/tcp" 2>/dev/null || true
done
sleep 1

# Receiver (listens :9003)
CG_RF_EXPECTED_N="$N_PTS" ./cg_rf_opa_receiver "$N_PTS" "$D" $PB >"$LOG_DIR/opa_receiver.log" 2>&1 &
PID_R=$!; sleep 4

# RF_R looping (connects :9003, listens :9002)
CG_RF_EXPECTED_N="$N_PTS" ./cg_rf_receiver_firewall_opa >"$LOG_DIR/opa_rf_receiver.log" 2>&1 &
PID_RFR=$!; sleep 3

# RF_S looping (connects :9002, listens :9001)
CG_RF_EXPECTED_N="$N_PTS" ./cg_rf_sender_firewall_opa >"$LOG_DIR/opa_rf_sender.log" 2>&1 &
PID_RFS=$!; sleep 2

# Sender (connects :9001)
CG_RF_EXPECTED_N="$N_PTS" ./cg_rf_opa_sender "$N_PTS" "$D" $PA -- $RA >"$LOG_DIR/opa_sender.log" 2>&1 &
PID_S=$!

for item in \
    "$PID_R:$LOG_DIR/opa_receiver.log:OPA receiver" \
    "$PID_RFR:$LOG_DIR/opa_rf_receiver.log:receiver firewall" \
    "$PID_RFS:$LOG_DIR/opa_rf_sender.log:sender firewall" \
    "$PID_S:$LOG_DIR/opa_sender.log:OPA sender"; do
    IFS=: read -r pid file label <<<"$item"
    if ! wait "$pid"; then
        echo "ERROR: $label failed. Log follows:" >&2
        cat "$file" >&2 2>/dev/null || true
        exit 1
    fi
done

echo "=== OPA Results ==="
cat "$LOG_DIR/opa_receiver.log"
