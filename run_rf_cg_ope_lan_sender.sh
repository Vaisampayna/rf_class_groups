#!/usr/bin/env bash
# Run the sender side of RF-OPE on System A.
#
# Usage: bash run_rf_cg_ope_lan_sender.sh <System_B_IP> <degree>

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_ope_sender" ]]; then
    "$ROOT/build_local.sh"
fi
cd "$BUILD_DIR"

SYSTEM_B_IP="${1:-127.0.0.1}"
DEGREE="${2:-1000}"
shift 2 || true
export CG_RF_LANES="${CG_RF_LANES:-4}"
export CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-3600}"
DEFAULT_THREADS=$(( ($(nproc) + 1) / 2 ))
if (( DEFAULT_THREADS < 1 )); then DEFAULT_THREADS=1; fi
export CG_RF_THREADS="${CG_RF_THREADS:-$DEFAULT_THREADS}"

LOG_DIR="$ROOT/logs/ope_sender"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/ope_sender.log "$LOG_DIR"/ope_rf_sender.log

cleanup_children() {
    jobs -pr | xargs -r kill 2>/dev/null || true
}
trap cleanup_children EXIT

if [[ "${CG_SKIP_PORT_CLEANUP:-0}" != "1" ]]; then
    fuser -k "9001/tcp" 2>/dev/null || true
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
        if (( waited == 10 )); then
            echo "Still waiting for $label. Check System B is listening on ${SYSTEM_B_IP}:9002." >&2
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

echo "=== System A: RF-OPE Sender & S-RF ==="
echo "Degree: $DEGREE"
echo "Transport lanes: $CG_RF_LANES"
echo "Crypto worker threads per process: $CG_RF_THREADS"
echo "Connect retries: $CG_CONNECT_RETRIES"

./cg_rf_sender_firewall_opa "$SYSTEM_B_IP" >"$LOG_DIR/ope_rf_sender.log" 2>&1 &
PID_RFS=$!
wait_for_log "$LOG_DIR/ope_rf_sender.log" "listening on :9001" "$PID_RFS" "sender firewall"

./cg_rf_ope_sender "$SYSTEM_B_IP" "$DEGREE" "$@" >"$LOG_DIR/ope_sender.log" 2>&1 &
PID_S=$!

wait_or_report "$PID_RFS" "sender firewall" "$LOG_DIR/ope_rf_sender.log"
wait_or_report "$PID_S" "RF-OPE sender" "$LOG_DIR/ope_sender.log"

echo "=== RF-OPE Sender Results ==="
cat "$LOG_DIR/ope_sender.log"
echo ""
echo "=== Timing Details ==="
grep -R "pre-warmed\\|mask sampling\\|protocol/RF-OLE\\|protocol end-to-end\\|ciphertext\\|RF-OLE" \
    "$LOG_DIR"/ope_*.log || true
