#!/usr/bin/env bash
set -euo pipefail

# Sweep the current two-way RF-PSI construction:
#   one-way RF-PSI + encrypted reveal-back phase.
#
# This script is intentionally separate from benchmark_2pc_cg_sweep.sh so a
# long PSI2 sweep can be resumed or rerun without disturbing the OLE/OPE/OPA/PSI
# table.  It preserves the same paper-facing settings used by the 128-bit CG
# sweep unless the caller overrides them.

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

SIZES=("$@")
if [[ "$#" -eq 0 ]]; then
    SIZES=(1024 2048 4096 8192 16384 32768)
fi

STAMP="$(TZ=UTC date +%Y%m%d_%H%M%S_UTC)"
SWEEP_DIR="${OUT_DIR:-$ROOT/benchmark_2pc_rf_psi2_sweep_${STAMP}}"
mkdir -p "$SWEEP_DIR"

export LOCAL_IP="${LOCAL_IP:-PARTY_B_IP}"
export REMOTE_IP="${REMOTE_IP:-PARTY_A_IP}"
export LOCAL_USER="${LOCAL_USER:-party_b_user}"
export REMOTE_USER="${REMOTE_USER:-party_a_user}"
export LOCAL_ROOT="${LOCAL_ROOT:-/path/to/party_b/reverse_firewall_cg}"
export REMOTE_ROOT="${REMOTE_ROOT:-/path/to/party_a/reverse_firewall_cg}"
export BUILD_DIR="${BUILD_DIR:-build-2pc}"
export CHECKER_DIR="${CHECKER_DIR:-$ROOT/build-portable}"

export CG_Q_NBITS="${CG_Q_NBITS:-128}"
export CG_K="${CG_K:-1}"
export CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}"
export CG_PSI_INPUT_BITS="${CG_PSI_INPUT_BITS:-128}"
export CG_RF_LANES="${CG_RF_LANES:-8}"
export CG_RF_THREADS_LOCAL="${CG_RF_THREADS_LOCAL:-28}"
export CG_RF_THREADS_REMOTE="${CG_RF_THREADS_REMOTE:-32}"
export CG_RF_THREADS_PSI2_LOCAL="${CG_RF_THREADS_PSI2_LOCAL:-14}"
export CG_RF_THREADS_PSI2_REMOTE="${CG_RF_THREADS_PSI2_REMOTE:-16}"
export CG_RF_CHUNK_SIZE="${CG_RF_CHUNK_SIZE:-128}"
export CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-7200}"
export TIMEOUT_S="${TIMEOUT_S:-7200}"

printf 'run,side,component,protocol_time_ms,source\n' >"$SWEEP_DIR/protocol_times.csv"
printf 'name,local_rc,remote_rc,outer_elapsed_s,logs\n' >"$SWEEP_DIR/runs.csv"
printf 'name,checker_rc,checker_log\n' >"$SWEEP_DIR/checks.csv"

merge_run() {
    local n="$1"
    local run_dir="$2"
    local row

    if [[ -f "$run_dir/protocol_times.csv" ]]; then
        while IFS= read -r row; do
            [[ "$row" == run,* ]] && continue
            [[ -z "$row" ]] && continue
            printf '%s\n' "$row" >>"$SWEEP_DIR/protocol_times.csv"
        done <"$run_dir/protocol_times.csv"
    fi
    if [[ -f "$run_dir/runs.csv" ]]; then
        tail -n +2 "$run_dir/runs.csv" >>"$SWEEP_DIR/runs.csv"
    fi
    if [[ -f "$run_dir/checks.csv" ]]; then
        tail -n +2 "$run_dir/checks.csv" >>"$SWEEP_DIR/checks.csv"
    fi
}

FAILED=0
for n in "${SIZES[@]}"; do
    run_dir="$SWEEP_DIR/run_${n}"
    echo "==> rf_psi2_reveal_${n}"
    set +e
    OUT_DIR="$run_dir" bash ./run_2pc_rf_psi2.sh "$n"
    rc=$?
    set -e
    merge_run "$n" "$run_dir"
    if (( rc != 0 )); then
        FAILED=1
        echo "WARNING: rf_psi2_reveal_${n} returned rc=${rc}; merged available artifacts and continuing." >&2
    fi
done

echo "PSI2 sweep logs: $SWEEP_DIR"
echo "Combined PSI2 party timings: $SWEEP_DIR/protocol_times.csv"
exit "$FAILED"
