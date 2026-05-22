#!/usr/bin/env bash
# run_rf_cg_psi2_lan_a.sh — Party A for two-way RF-PSI.
#
# This is implemented as:
#   1. The existing one-way RF-PSI where Party B learns S_A ∩ S_B.
#   2. A reveal-back phase where Party A sends a fresh public key through
#      S-RF and R-RF, Party B encrypts the intersection, and the firewalls
#      inverse-maul/rerandomize the ciphertexts back to Party A.
#
# File input:
#   CG_RF_LANES=4 bash run_rf_cg_psi2_lan_a.sh <Party_B_IP> --file set_A.txt <m_B>

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_psi_sender" || ! -x "$BUILD_DIR/cg_rf_psi2_reveal_sender" ]]; then
    "$ROOT/build_local.sh"
fi
cd "$BUILD_DIR"

PARTY_B_IP="${1:-127.0.0.1}"
shift || true
if [[ "${1:-}" != "--file" || $# -lt 3 ]]; then
    echo "Usage: $0 <Party_B_IP> --file set_A.txt <m_B>" >&2
    exit 2
fi
SET_A_FILE="$2"
M_B="$3"
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
rm -f "$LOG_DIR"/psi2_A*.log "$LOG_DIR"/psi2_a_*.log

cleanup_children() {
    jobs -pr | xargs -r kill 2>/dev/null || true
}
trap cleanup_children EXIT

if [[ "${CG_SKIP_PORT_CLEANUP:-0}" != "1" ]]; then
    for port in "$CG_PSI2_FOPA1_RFS" "$CG_PSI2_REVEAL_RFS"; do
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

echo "=== Party A: two-way RF-PSI sender side ==="
echo "Party B IP: $PARTY_B_IP"
echo "Transport lanes: $CG_RF_LANES"
echo "Crypto worker threads per process: $CG_RF_THREADS"
echo "One-way RF-PSI ports: $CG_PSI2_FOPA1_RFS/$CG_PSI2_FOPA1_RFR/$CG_PSI2_FOPA1_REC"
echo "Reveal-back ports: $CG_PSI2_REVEAL_RFS/$CG_PSI2_REVEAL_RFR/$CG_PSI2_REVEAL_REC"

# Phase 1: A acts as the one-way PSI sender.
CG_PORT_RFR="$CG_PSI2_FOPA1_RFR" CG_PORT_RFS="$CG_PSI2_FOPA1_RFS" \
    ./cg_rf_sender_firewall_opa "$PARTY_B_IP" >"$LOG_DIR/psi2_a_srf_fopa1.log" 2>&1 &
PID_SRF1=$!
wait_for_log "$LOG_DIR/psi2_a_srf_fopa1.log" "listening on :$CG_PSI2_FOPA1_RFS" "$PID_SRF1" "A S-RF for one-way PSI"

CG_PROTOCOL_TIMING_FILE="$LOG_DIR/psi2_A_oneway_time.csv" \
    ./cg_rf_psi_sender "$M_B" --input-file "$SET_A_FILE" >"$LOG_DIR/psi2_A_oneway.log" 2>&1 &
PID_SEND=$!
wait_or_report "$PID_SEND" "Party A one-way PSI sender" "$LOG_DIR/psi2_A_oneway.log"
wait_or_report "$PID_SRF1" "A S-RF for one-way PSI" "$LOG_DIR/psi2_a_srf_fopa1.log"

# Phase 2: A receives Party B's intersection through the reveal-back RF path.
CG_PSI2_REVEAL_RFR="$CG_PSI2_REVEAL_RFR" CG_PSI2_REVEAL_RFS="$CG_PSI2_REVEAL_RFS" \
    ./cg_rf_psi2_reveal_srf "$PARTY_B_IP" >"$LOG_DIR/psi2_a_srf_reveal.log" 2>&1 &
PID_SRF_REVEAL=$!
wait_for_log "$LOG_DIR/psi2_a_srf_reveal.log" "listening on :$CG_PSI2_REVEAL_RFS" "$PID_SRF_REVEAL" "A S-RF reveal"

CG_PROTOCOL_TIMING_FILE="$LOG_DIR/psi2_A_reveal_time.csv" \
    ./cg_rf_psi2_reveal_sender --output-file "$LOG_DIR/psi2_A_intersection.txt" >"$LOG_DIR/psi2_A.log" 2>&1 &
PID_REVEAL=$!
wait_or_report "$PID_REVEAL" "Party A reveal endpoint" "$LOG_DIR/psi2_A.log"
wait_or_report "$PID_SRF_REVEAL" "A S-RF reveal" "$LOG_DIR/psi2_a_srf_reveal.log"
write_combined_timing "$PARTY_TIMING_FILE" "psi2_A" \
    "$LOG_DIR/psi2_A_oneway_time.csv" "$LOG_DIR/psi2_A_reveal_time.csv"

echo "=== Party A Results ==="
cat "$LOG_DIR/psi2_A.log"
echo ""
echo "=== Party A Timing ==="
grep -E "done in|total intersection|forwarded|expecting" \
    "$LOG_DIR"/psi2_A*.log "$LOG_DIR"/psi2_a_*.log || true
