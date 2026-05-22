#!/usr/bin/env bash
set -euo pipefail

# Sweep direct two-way PSI:
#   one direct PSI/FOPA call lets Party B learn S_A cap S_B, then Party B sends
#   the intersection directly back to Party A in the clear.
#
# Defaults cover N = 2^10,...,2^15.  Pass explicit sizes to override.

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

SIZES=("$@")
if [[ "$#" -eq 0 ]]; then
    SIZES=(1024 2048 4096 8192 16384 32768)
fi

STAMP="$(TZ=UTC date +%Y%m%d_%H%M%S_UTC)"
SWEEP_DIR="${OUT_DIR:-$ROOT/benchmark_2pc_direct_psi2_sweep_${STAMP}}"
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
export CG_RF_THREADS_LOCAL="${CG_RF_THREADS_LOCAL:-28}"
export CG_RF_THREADS_REMOTE="${CG_RF_THREADS_REMOTE:-32}"
export CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-7200}"
export TIMEOUT_S="${TIMEOUT_S:-7200}"

printf 'run,side,component,protocol_time_ms,source\n' >"$SWEEP_DIR/protocol_times.csv"
printf 'name,local_rc,remote_rc,outer_elapsed_s,logs\n' >"$SWEEP_DIR/runs.csv"
printf 'name,checker_rc,checker_log\n' >"$SWEEP_DIR/checks.csv"

merge_run() {
    local run_dir="$1"
    local row
    if [[ -f "$run_dir/protocol_times.csv" ]]; then
        while IFS= read -r row; do
            [[ "$row" == run,* || -z "$row" ]] && continue
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
    echo "==> direct_psi2_${n}"
    set +e
    OUT_DIR="$run_dir" bash ./run_2pc_direct_psi2.sh "$n"
    rc=$?
    set -e
    merge_run "$run_dir"
    if (( rc != 0 )); then
        FAILED=1
        echo "WARNING: direct_psi2_${n} returned rc=${rc}; merged available artifacts and continuing." >&2
    fi
done

echo "Direct PSI2 sweep logs: $SWEEP_DIR"
echo "Combined direct PSI2 party timings: $SWEEP_DIR/protocol_times.csv"
exit "$FAILED"
