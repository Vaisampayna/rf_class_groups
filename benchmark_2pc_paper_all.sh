#!/usr/bin/env bash
set -euo pipefail

# Run the full two-machine benchmark suite used for the paper table.
#
# The controller launches both protocol machines over SSH.  Configure the
# machine-specific values through environment variables, for example:
#
#   LOCAL_IP=... REMOTE_IP=... LOCAL_USER=... REMOTE_USER=... \
#   LOCAL_ROOT=... REMOTE_ROOT=... BUILD_DIR=build-2pc \
#   CHECKER_DIR="$PWD/build-portable" bash benchmark_2pc_paper_all.sh
#
# By default this runs N=2^10,...,2^15.  Pass explicit sizes to override.

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

SIZES=("$@")
if [[ "$#" -eq 0 ]]; then
    SIZES=(1024 2048 4096 8192 16384 32768)
fi

STAMP="$(TZ=UTC date +%Y%m%d_%H%M%S_UTC)"
SUITE_DIR="${OUT_DIR:-$ROOT/benchmark_2pc_paper_all_${STAMP}}"
mkdir -p "$SUITE_DIR"

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
export CG_FIXED_Q="${CG_FIXED_Q:-170141183460469232709364739622490341377}"
export CG_USE_NTT_POLY="${CG_USE_NTT_POLY:-1}"
export CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}"
export CG_PSI_INPUT_BITS="${CG_PSI_INPUT_BITS:-128}"
export CG_INPUT_FILE_BITS="${CG_INPUT_FILE_BITS:-128}"
export CG_RF_LANES="${CG_RF_LANES:-8}"
export CG_RF_THREADS_LOCAL="${CG_RF_THREADS_LOCAL:-28}"
export CG_RF_THREADS_REMOTE="${CG_RF_THREADS_REMOTE:-32}"
export CG_RF_CHUNK_SIZE="${CG_RF_CHUNK_SIZE:-128}"
export CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-7200}"
export CG_SKIP_CHECKS="${CG_SKIP_CHECKS:-0}"
export TIMEOUT_S="${TIMEOUT_S:-7200}"

PROTOCOLS=(
    direct_ole
    rf_ole
    direct_ole3
    rf_ole3
    direct_ope
    rf_ope
    direct_opa
    rf_opa
    direct_psi
    rf_psi
    direct_psi2
    rf_psi2
)

script_for() {
    case "$1" in
        direct_ole)  printf './run_2pc_direct_ole.sh' ;;
        rf_ole)      printf './run_2pc_rf_ole.sh' ;;
        direct_ole3) printf './run_2pc_direct_ole3.sh' ;;
        rf_ole3)     printf './run_2pc_rf_ole3.sh' ;;
        direct_ope)  printf './run_2pc_direct_ope.sh' ;;
        rf_ope)      printf './run_2pc_rf_ope.sh' ;;
        direct_opa)  printf './run_2pc_direct_opa.sh' ;;
        rf_opa)      printf './run_2pc_rf_opa.sh' ;;
        direct_psi)  printf './run_2pc_direct_psi.sh' ;;
        rf_psi)      printf './run_2pc_rf_psi.sh' ;;
        direct_psi2) printf './run_2pc_direct_psi2.sh' ;;
        rf_psi2)     printf './run_2pc_rf_psi2.sh' ;;
        *)           return 2 ;;
    esac
}

printf 'run,side,component,protocol_time_ms,source\n' >"$SUITE_DIR/protocol_times.csv"
printf 'name,local_rc,remote_rc,outer_elapsed_s,logs\n' >"$SUITE_DIR/runs.csv"
printf 'name,checker_rc,checker_log\n' >"$SUITE_DIR/checks.csv"

merge_run() {
    local run_dir="$1"
    local row
    if [[ -f "$run_dir/protocol_times.csv" ]]; then
        while IFS= read -r row; do
            [[ "$row" == run,* || -z "$row" ]] && continue
            printf '%s\n' "$row" >>"$SUITE_DIR/protocol_times.csv"
        done <"$run_dir/protocol_times.csv"
    fi
    if [[ -f "$run_dir/runs.csv" ]]; then
        tail -n +2 "$run_dir/runs.csv" >>"$SUITE_DIR/runs.csv"
    fi
    if [[ -f "$run_dir/checks.csv" ]]; then
        tail -n +2 "$run_dir/checks.csv" >>"$SUITE_DIR/checks.csv"
    fi
}

FAILED=0
for n in "${SIZES[@]}"; do
    for proto in "${PROTOCOLS[@]}"; do
        run_dir="$SUITE_DIR/${proto}_${n}"
        script="$(script_for "$proto")"
        echo "==> ${proto}_${n}"
        set +e
        OUT_DIR="$run_dir" "$script" "$n"
        rc=$?
        set -e
        merge_run "$run_dir"
        if (( rc != 0 )); then
            FAILED=1
            echo "WARNING: ${proto}_${n} returned rc=${rc}; merged available artifacts and continuing." >&2
        fi
    done
done

echo "Paper benchmark logs: $SUITE_DIR"
echo "Combined party timings: $SUITE_DIR/protocol_times.csv"
echo "Controller wall-clock times: $SUITE_DIR/runs.csv"
echo "Correctness checks: $SUITE_DIR/checks.csv"
exit "$FAILED"
