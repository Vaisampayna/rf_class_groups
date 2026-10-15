#!/usr/bin/env bash
# Run direct CG-AHE OLE-backed PSI on localhost.
#
# Usage:
#   CG_RF_LANES=8 CG_RF_THREADS=12 bash run_cg_psi_local.sh 1000 1000
#   bash run_cg_psi_local.sh --files set_A.txt set_B.txt true_intersection.txt

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
source "$ROOT/scripts/run_common.sh"
cg_run_init local
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build}"
ARG1="${1:-5}"
ARG2="${2:-5}"
PORT="${CG_PSI_PORT_REC:-9106}"
export CG_RF_LANES="${CG_RF_LANES:-4}"
export CG_Q_NBITS="${CG_Q_NBITS:-128}"
export CG_K="${CG_K:-1}"
export CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}"

if [[ ! -x "$BUILD_DIR/cg_psi_receiver" || ! -x "$BUILD_DIR/cg_psi_sender" || ! -x "$BUILD_DIR/cg_print_q" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target cg_psi_receiver cg_psi_sender cg_print_q -j
fi

LOG_DIR="$CG_RUN_LOG_DIR/psi_direct"
INPUT_DIR="${CG_PSI_INPUT_DIR:-$ROOT/inputs/psi_direct}"
mkdir -p "$LOG_DIR"
rm -f "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log "$LOG_DIR"/intersection_out.txt
sleep 1

if [[ "$ARG1" == "--files" ]]; then
    SET_A_FILE="${2:?missing set_A file}"
    SET_B_FILE="${3:?missing set_B file}"
    TRUE_FILE="${4:?missing true intersection file}"
elif [[ "$ARG1" =~ ^[0-9]+$ ]] && [[ "$ARG2" =~ ^[0-9]+$ ]]; then
    MA="$ARG1"
    MB="$ARG2"
    OVERLAP="${3:-$(( MA < MB ? MA / 5 : MB / 5 ))}"
    SEED="${4:-42}"
    mkdir -p "$INPUT_DIR"
    SET_A_FILE="$INPUT_DIR/set_A.txt"
    SET_B_FILE="$INPUT_DIR/set_B.txt"
    TRUE_FILE="$INPUT_DIR/true_intersection.txt"
    python3 "$ROOT/generate_sets.py" "$MA" "$MB" "$OVERLAP" \
        "$SET_A_FILE" "$SET_B_FILE" "$TRUE_FILE" "$SEED" "${CG_PSI_INPUT_BITS:-128}" "$("$BUILD_DIR/cg_print_q")"
else
    echo "Usage: $0 [m_A m_B [overlap] [seed]] OR --files set_A.txt set_B.txt true_intersection.txt" >&2
    exit 2
fi

MA="$(wc -w <"$SET_A_FILE")"
MB="$(wc -w <"$SET_B_FILE")"
OUT_FILE="$LOG_DIR/intersection_out.txt"
echo "=== Direct PSI over batched OLE: file inputs |S_A|=$MA, |S_B|=$MB, lanes=$CG_RF_LANES ==="

"$BUILD_DIR/cg_psi_receiver" "$MA" --input-file "$SET_B_FILE" \
    --output-file "$OUT_FILE" --port "$PORT" >"$LOG_DIR/receiver.log" 2>&1 &
PID_R=$!
sleep 1

"$BUILD_DIR/cg_psi_sender" 127.0.0.1 "$MB" --input-file "$SET_A_FILE" \
    --port "$PORT" >"$LOG_DIR/sender.log" 2>&1 &
PID_S=$!

cg_wait_all "$PID_R" "$PID_S"

echo "=== Direct PSI Results ==="
cat "$LOG_DIR/receiver.log"
python3 "$ROOT/check_correctness.py" "$TRUE_FILE" "$OUT_FILE"
echo ""
echo "=== Timing Details ==="
grep -E "done in|OPTIME|total intersection|process end-to-end" "$LOG_DIR"/receiver.log "$LOG_DIR"/sender.log || true
