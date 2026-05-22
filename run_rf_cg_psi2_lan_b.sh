#!/usr/bin/env bash
# run_rf_cg_psi2_lan_b.sh — Party B for two-way RF-PSI.
#
# Start this script first. Party B runs the normal one-way RF-PSI receiver,
# learns S_A ∩ S_B, then encrypts that intersection back to Party A through a
# fresh key-mauling path protected by both reverse firewalls.
#
# File input:
#   CG_RF_LANES=4 bash run_rf_cg_psi2_lan_b.sh <Party_A_IP> --file <m_A> set_B.txt

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_psi_receiver" || ! -x "$BUILD_DIR/cg_rf_psi2_reveal_receiver" ]]; then
    "$ROOT/build_local.sh"
fi
cd "$BUILD_DIR"

PARTY_A_IP="${1:-127.0.0.1}"
shift || true
if [[ "${1:-}" != "--file" || $# -lt 3 ]]; then
    echo "Usage: $0 <Party_A_IP> --file <m_A> set_B.txt" >&2
    exit 2
fi
M_A="$2"
SET_B_FILE="$3"
PARTY_TIMING_FILE="${CG_PROTOCOL_TIMING_FILE:-}"

export CG_RF_LANES="${CG_RF_LANES:-4}"
export CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-3600}"
export CG_Q_NBITS="${CG_Q_NBITS:-128}"
export CG_K="${CG_K:-1}"
export CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}"
DEFAULT_THREADS=$(( ($(nproc) + 1) / 2 ))
if (( DEFAULT_THREADS < 1 )); then DEFAULT_THREADS=1; fi
export CG_RF_THREADS="${CG_RF_THREADS:-$DEFAULT_THREADS}"

export CG_PSI2_FOPA1_REC="${CG_PSI2_FOPA1_REC:-9003}"
export CG_PSI2_FOPA1_RFR="${CG_PSI2_FOPA1_RFR:-9002}"
export CG_PSI2_FOPA1_RFS="${CG_PSI2_FOPA1_RFS:-9001}"
export CG_PSI2_REVEAL_REC="${CG_PSI2_REVEAL_REC:-9043}"
export CG_PSI2_REVEAL_RFR="${CG_PSI2_REVEAL_RFR:-9042}"
export CG_PSI2_REVEAL_RFS="${CG_PSI2_REVEAL_RFS:-9041}"

LOG_DIR="$ROOT/logs"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/psi2_B*.log "$LOG_DIR"/psi2_b_*.log

cleanup_children() {
    jobs -pr | xargs -r kill 2>/dev/null || true
}
trap cleanup_children EXIT

if [[ "${CG_SKIP_PORT_CLEANUP:-0}" != "1" ]]; then
    for port in "$CG_PSI2_FOPA1_REC" "$CG_PSI2_FOPA1_RFR" \
                "$CG_PSI2_REVEAL_REC" "$CG_PSI2_REVEAL_RFR"; do
        fuser -k "$port/tcp" 2>/dev/null || true
    done
    sleep 1
fi

wait_for_log() {
    local file="$1"
    local pattern="$2"
    local pid="$3"
    local label="$4"
    local timeout_s="${5:-900}"
    local waited=0
    while (( waited < timeout_s )); do
        if [[ -f "$file" ]] && grep -q "$pattern" "$file"; then
            return 0
        fi
        if ! kill -0 "$pid" 2>/dev/null; then
            echo "ERROR: $label exited before becoming ready. Log follows:" >&2
            cat "$file" >&2 2>/dev/null || true
            exit 1
        fi
        sleep 1
        waited=$((waited + 1))
    done
    echo "ERROR: timed out waiting for $label readiness pattern: $pattern" >&2
    cat "$file" >&2 2>/dev/null || true
    exit 1
}

wait_or_report() {
    local pid="$1"
    local label="$2"
    local file="$3"
    set +e
    wait "$pid"
    local status=$?
    set -e
    if (( status != 0 )); then
        echo "ERROR: $label failed with status $status. Log follows:" >&2
        cat "$file" >&2 2>/dev/null || true
        exit "$status"
    fi
}

write_combined_timing() {
    local out="$1"
    local component="$2"
    local first="$3"
    local second="$4"
    if [[ -z "$out" ]]; then
        return 0
    fi
    local total
    total=$(awk -F, 'NR > 1 { s += $3 } END { printf "%.6f", s }' "$first" "$second")
    {
        echo "component,metric,value_ms"
        echo "$component,protocol_time_excluding_input_and_output_files,$total"
    } >"$out"
}

echo "=== Party B: two-way RF-PSI receiver side ==="
echo "Party A IP: $PARTY_A_IP"
echo "Transport lanes: $CG_RF_LANES"
echo "Crypto worker threads per process: $CG_RF_THREADS"
echo "One-way RF-PSI ports: $CG_PSI2_FOPA1_RFS/$CG_PSI2_FOPA1_RFR/$CG_PSI2_FOPA1_REC"
echo "Reveal-back ports: $CG_PSI2_REVEAL_RFS/$CG_PSI2_REVEAL_RFR/$CG_PSI2_REVEAL_REC"

INTERSECTION_FILE="$LOG_DIR/psi2_B_intersection.txt"

# Phase 1: B acts as the one-way PSI receiver and learns the intersection.
CG_PORT_REC="$CG_PSI2_FOPA1_REC" \
    CG_PROTOCOL_TIMING_FILE="$LOG_DIR/psi2_B_oneway_time.csv" \
    ./cg_rf_psi_receiver "$M_A" --input-file "$SET_B_FILE" \
        --output-file "$INTERSECTION_FILE" >"$LOG_DIR/psi2_B_oneway.log" 2>&1 &
PID_RECV=$!
wait_for_log "$LOG_DIR/psi2_B_oneway.log" "listening on :$CG_PSI2_FOPA1_REC" "$PID_RECV" "B one-way PSI receiver"

CG_PORT_REC="$CG_PSI2_FOPA1_REC" CG_PORT_RFR="$CG_PSI2_FOPA1_RFR" \
    ./cg_rf_receiver_firewall_opa >"$LOG_DIR/psi2_b_rrf_fopa1.log" 2>&1 &
PID_RRF1=$!
wait_for_log "$LOG_DIR/psi2_b_rrf_fopa1.log" "listening on :$CG_PSI2_FOPA1_RFR" "$PID_RRF1" "B R-RF for one-way PSI"

wait_or_report "$PID_RECV" "Party B one-way PSI receiver" "$LOG_DIR/psi2_B_oneway.log"
wait_or_report "$PID_RRF1" "B R-RF for one-way PSI" "$LOG_DIR/psi2_b_rrf_fopa1.log"

# Phase 2: B encrypts the learned intersection to A's mauled reveal key.
CG_PSI2_REVEAL_REC="$CG_PSI2_REVEAL_REC" \
    CG_PROTOCOL_TIMING_FILE="$LOG_DIR/psi2_B_reveal_time.csv" \
    ./cg_rf_psi2_reveal_receiver --input-file "$INTERSECTION_FILE" >"$LOG_DIR/psi2_B.log" 2>&1 &
PID_REVEAL_RECV=$!
wait_for_log "$LOG_DIR/psi2_B.log" "listening on :$CG_PSI2_REVEAL_REC" "$PID_REVEAL_RECV" "B reveal endpoint"

CG_PSI2_REVEAL_REC="$CG_PSI2_REVEAL_REC" CG_PSI2_REVEAL_RFR="$CG_PSI2_REVEAL_RFR" \
    ./cg_rf_psi2_reveal_rrf >"$LOG_DIR/psi2_b_rrf_reveal.log" 2>&1 &
PID_RRF_REVEAL=$!
wait_for_log "$LOG_DIR/psi2_b_rrf_reveal.log" "listening on :$CG_PSI2_REVEAL_RFR" "$PID_RRF_REVEAL" "B R-RF reveal"

wait_or_report "$PID_REVEAL_RECV" "Party B reveal endpoint" "$LOG_DIR/psi2_B.log"
wait_or_report "$PID_RRF_REVEAL" "B R-RF reveal" "$LOG_DIR/psi2_b_rrf_reveal.log"
write_combined_timing "$PARTY_TIMING_FILE" "psi2_B" \
    "$LOG_DIR/psi2_B_oneway_time.csv" "$LOG_DIR/psi2_B_reveal_time.csv"

echo "=== Party B Results ==="
cat "$LOG_DIR/psi2_B.log"
echo ""
echo "=== Party B Timing ==="
grep -E "done in|total intersection|forwarded|read" \
    "$LOG_DIR"/psi2_B*.log "$LOG_DIR"/psi2_b_*.log || true
