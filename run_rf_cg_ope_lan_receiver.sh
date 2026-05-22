#!/usr/bin/env bash
# Run the receiver side of RF-OPE on System B.
#
# Usage: bash run_rf_cg_ope_lan_receiver.sh <degree>

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_ope_receiver" ]]; then
    "$ROOT/build_local.sh"
fi
cd "$BUILD_DIR"

DEGREE="${1:-1000}"
shift || true
export CG_RF_LANES="${CG_RF_LANES:-4}"
export CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-3600}"
DEFAULT_THREADS=$(( ($(nproc) + 1) / 2 ))
if (( DEFAULT_THREADS < 1 )); then DEFAULT_THREADS=1; fi
export CG_RF_THREADS="${CG_RF_THREADS:-$DEFAULT_THREADS}"

LOG_DIR="$ROOT/logs/ope_receiver"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/ope_receiver.log "$LOG_DIR"/ope_rf_receiver.log

cleanup_children() {
    jobs -pr | xargs -r kill 2>/dev/null || true
}
trap cleanup_children EXIT

if [[ "${CG_SKIP_PORT_CLEANUP:-0}" != "1" ]]; then
    for port in 9002 9003 9004; do
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

echo "=== System B: RF-OPE Receiver & R-RF ==="
echo "Degree: $DEGREE"
echo "Transport lanes: $CG_RF_LANES"
echo "Crypto worker threads per process: $CG_RF_THREADS"
echo "Connect retries: $CG_CONNECT_RETRIES"

./cg_rf_ope_receiver "$DEGREE" "$@" >"$LOG_DIR/ope_receiver.log" 2>&1 &
PID_R=$!

./cg_rf_receiver_firewall_opa >"$LOG_DIR/ope_rf_receiver.log" 2>&1 &
PID_RFR=$!

wait_for_log "$LOG_DIR/ope_receiver.log" "listening on :9003" "$PID_R" "RF-OPE receiver"
wait_for_log "$LOG_DIR/ope_rf_receiver.log" "listening on :9002" "$PID_RFR" "receiver firewall"

wait_or_report "$PID_RFR" "receiver firewall" "$LOG_DIR/ope_rf_receiver.log"
wait_or_report "$PID_R" "RF-OPE receiver" "$LOG_DIR/ope_receiver.log"

echo "=== RF-OPE Receiver Results ==="
cat "$LOG_DIR/ope_receiver.log"
echo ""
echo "=== Timing Details ==="
grep -R "pre-warmed\\|setup/evaluation\\|protocol/RF-OLE\\|Horner\\|protocol end-to-end\\|ciphertext\\|RF-OLE" \
    "$LOG_DIR"/ope_*.log || true
