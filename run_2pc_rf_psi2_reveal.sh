#!/usr/bin/env bash
# Run two-way RF-PSI from the controller machine.
# <controller-ip>.
#
# Protocol construction:
#   one-way RF-PSI + encrypted reveal-back phase
#
# Machines:
#   Party B / receiver side: <party-b-ip>  user <party-b-user>
#   Party A / sender side:   <party-a-ip>  user <party-a-user>
#   Controller:              <controller-ip>  user <controller-user>
#
# Usage:
#   bash run_2pc_rf_psi2_reveal.sh [N]
#
# Examples:
#   bash run_2pc_rf_psi2_reveal.sh 1000
#   OVERLAP=200 bash run_2pc_rf_psi2_reveal.sh 1000
#   SEED=12345 CG_PSI_INPUT_BITS=128 bash run_2pc_rf_psi2_reveal.sh 4096

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

N="${1:-1000}"

LOCAL_IP="${LOCAL_IP:-<party-b-ip>}" \
REMOTE_IP="${REMOTE_IP:-<party-a-ip>}" \
LOCAL_USER="${LOCAL_USER:-<party-b-user>}" \
REMOTE_USER="${REMOTE_USER:-<party-a-user>}" \
LOCAL_ROOT="${LOCAL_ROOT:-<party-b-repo-path>}" \
REMOTE_ROOT="${REMOTE_ROOT:-<party-a-repo-path>}" \
BUILD_DIR="${BUILD_DIR:-build-2pc}" \
CHECKER_DIR="${CHECKER_DIR:-$ROOT/build-portable}" \
CG_Q_NBITS="${CG_Q_NBITS:-128}" \
CG_K="${CG_K:-1}" \
CG_BENCH_INPUT_BITS="${CG_BENCH_INPUT_BITS:-128}" \
CG_PSI_INPUT_BITS="${CG_PSI_INPUT_BITS:-128}" \
CG_RF_LANES="${CG_RF_LANES:-8}" \
CG_RF_THREADS_LOCAL="${CG_RF_THREADS_LOCAL:-28}" \
CG_RF_THREADS_REMOTE="${CG_RF_THREADS_REMOTE:-32}" \
CG_RF_THREADS_PSI2_LOCAL="${CG_RF_THREADS_PSI2_LOCAL:-14}" \
CG_RF_THREADS_PSI2_REMOTE="${CG_RF_THREADS_PSI2_REMOTE:-16}" \
CG_RF_CHUNK_SIZE="${CG_RF_CHUNK_SIZE:-128}" \
CG_CONNECT_RETRIES="${CG_CONNECT_RETRIES:-7200}" \
TIMEOUT_S="${TIMEOUT_S:-7200}" \
bash ./run_2pc_rf_psi2.sh "$N"
