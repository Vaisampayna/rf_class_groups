#!/usr/bin/env bash
# Run the current two-way RF-PSI protocol from the controller machine
# CONTROLLER_IP.
#
# Protocol construction:
#   one-way RF-PSI + encrypted reveal-back phase
#
# Machines:
#   Party B / receiver side: PARTY_B_IP  user party_b_user
#   Party A / sender side:   PARTY_A_IP  user party_a_user
#   Controller:              CONTROLLER_IP  user anonymous
#
# Usage:
#   bash run_2pc_rf_psi2_reveal_from_190.sh [N]
#
# Examples:
#   bash run_2pc_rf_psi2_reveal_from_190.sh 1000
#   OVERLAP=200 bash run_2pc_rf_psi2_reveal_from_190.sh 1000
#   SEED=12345 CG_PSI_INPUT_BITS=128 bash run_2pc_rf_psi2_reveal_from_190.sh 4096

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
cd "$ROOT"

N="${1:-1000}"

LOCAL_IP="${LOCAL_IP:-PARTY_B_IP}" \
REMOTE_IP="${REMOTE_IP:-PARTY_A_IP}" \
LOCAL_USER="${LOCAL_USER:-party_b_user}" \
REMOTE_USER="${REMOTE_USER:-party_a_user}" \
LOCAL_ROOT="${LOCAL_ROOT:-/path/to/party_b/reverse_firewall_cg}" \
REMOTE_ROOT="${REMOTE_ROOT:-/path/to/party_a/reverse_firewall_cg}" \
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
