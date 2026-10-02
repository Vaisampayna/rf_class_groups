#!/usr/bin/env bash
set -euo pipefail

# Apply faster, non-paper-default runtime knobs to any run_2pc_*.sh command.
# Usage:
#   bash run_2pc_fast_preset.sh ./run_2pc_rf_opa.sh 8192
#
# Paper reproduction scripts keep their documented defaults. This helper is for
# additional "fastest practical run" experiments.

if [[ "$#" -lt 1 ]]; then
    echo "Usage: $0 <run_2pc_script> [args...]" >&2
    exit 2
fi

export CG_RF_LANES="${CG_RF_LANES:-8}"
export CG_RF_CHUNK_SIZE="${CG_RF_CHUNK_SIZE:-256}"
export CG_PROFILE_OPS="${CG_PROFILE_OPS:-0}"

# RF PSI2 launches more concurrent endpoint/firewall processes, so keep its
# split-thread defaults unless the caller has explicitly chosen otherwise.
export CG_RF_THREADS_LOCAL="${CG_RF_THREADS_LOCAL:-28}"
export CG_RF_THREADS_REMOTE="${CG_RF_THREADS_REMOTE:-32}"
export CG_RF_THREADS_PSI2_LOCAL="${CG_RF_THREADS_PSI2_LOCAL:-14}"
export CG_RF_THREADS_PSI2_REMOTE="${CG_RF_THREADS_PSI2_REMOTE:-16}"

exec "$@"

