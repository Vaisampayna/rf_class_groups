#!/usr/bin/env bash
# run_rf_cg_batch_ole_lan_sender.sh — Run Batched RF-OLE Sender and S-RF on System A
# Usage: bash run_rf_cg_batch_ole_lan_sender.sh <System_B_IP> <N>

set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
if [[ ! -x "$BUILD_DIR/cg_rf_batch_ole_sender" ]]; then
    "$ROOT/build_local.sh"
fi
cd "$BUILD_DIR"

SYSTEM_B_IP="${1:-127.0.0.1}"
N=${2:-1000}
export CG_RF_LANES="${CG_RF_LANES:-4}"
export CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-3600}"
DEFAULT_THREADS=$(( ($(nproc) + 1) / 2 ))
if (( DEFAULT_THREADS < 1 )); then DEFAULT_THREADS=1; fi
export CG_RF_THREADS="${CG_RF_THREADS:-$DEFAULT_THREADS}"

LOG_DIR="$ROOT/logs/batch_ole_sender"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/batch_ole_sender.log "$LOG_DIR"/batch_ole_rf_sender.log

cleanup_children() {
    jobs -pr | xargs -r kill 2>/dev/null || true
}
trap cleanup_children EXIT

if [[ "${CG_SKIP_PORT_CLEANUP:-0}" != "1" ]]; then
    for port in 9001; do
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
        if (( waited == 10 )); then
            echo "Still waiting for $label. If this is S-RF, check System B is listening on ${SYSTEM_B_IP}:9002." >&2
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

echo "=== System A: Batched Sender & S-RF ==="
echo "N: $N"
echo "Transport lanes: $CG_RF_LANES"
echo "Crypto worker threads per process: $CG_RF_THREADS"
echo "Connect retries: $CG_CONNECT_RETRIES"
echo "S-RF will connect to R-RF at ${SYSTEM_B_IP}:9002 and listen on port 9001 for Sender."
echo "Sender private inputs will be written locally when CG_BENCH_IO_DIR is set."

# RF_S looping (connects :9002, listens :9001)
./cg_rf_sender_firewall_opa "${SYSTEM_B_IP}" >"$LOG_DIR/batch_ole_rf_sender.log" 2>&1 &
PID_RFS=$!
wait_for_log "$LOG_DIR/batch_ole_rf_sender.log" "listening on :9001" "$PID_RFS" "sender firewall"

# Batched Sender (connects :9001)
./cg_rf_batch_ole_sender "$N" "$SYSTEM_B_IP" >"$LOG_DIR/batch_ole_sender.log" 2>&1 &
PID_S=$!

wait_or_report "$PID_RFS" "sender firewall" "$LOG_DIR/batch_ole_rf_sender.log"
wait_or_report "$PID_S" "batch OLE sender" "$LOG_DIR/batch_ole_sender.log"
echo "Sender pipeline finished."
cat "$LOG_DIR/batch_ole_sender.log"
echo ""
echo "=== Timing Details ==="
grep -R "pre-warmed\\|encrypting\\|sending\\|receiving\\|decrypting\\|ciphertext\\|RF-OLE protocol\\|Finished" "$LOG_DIR"/batch_ole_*.log || true
