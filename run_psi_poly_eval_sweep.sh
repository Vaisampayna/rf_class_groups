#!/usr/bin/env bash
# Compare PSI polynomial evaluation strategies over several set sizes.
#
# Usage:
#   CG_RF_THREADS=12 bash run_psi_poly_eval_sweep.sh
#   CG_RF_THREADS=32 bash run_psi_poly_eval_sweep.sh 64 128 256 512 1000

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${CG_BUILD_DIR:-$ROOT/build-portable}"

if [[ ! -x "$BUILD_DIR/cg_psi_poly_compare" ]]; then
    cmake -S "$ROOT" -B "$BUILD_DIR"
    cmake --build "$BUILD_DIR" --target cg_psi_poly_compare -j
fi

if [[ "$#" -gt 0 ]]; then
    SIZES=("$@")
else
    SIZES=(32 64 128 256 512 1000 1500 2000 3000 4000)
fi

OUT="$ROOT/logs/psi_poly_eval_sweep.csv"
mkdir -p "$(dirname "$OUT")"
: > "$OUT"

first=1
for n in "${SIZES[@]}"; do
    tmp="$("$BUILD_DIR/cg_psi_poly_compare" "$n" "$n" 42)"
    if [[ "$first" -eq 1 ]]; then
        printf '%s\n' "$tmp" >> "$OUT"
        first=0
    else
        printf '%s\n' "$tmp" | tail -n +2 >> "$OUT"
    fi
done

cat "$OUT"
