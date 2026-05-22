#!/usr/bin/env bash
set -euo pipefail

REMOTE_IP="${REMOTE_IP:-PARTY_A_IP}"
REMOTE_USER="${REMOTE_USER:-party_a_user}"
REMOTE="${REMOTE_USER}@${REMOTE_IP}"
REMOTE_ROOT="${REMOTE_ROOT:-/path/to/party_a}"

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LWE_BIN="${LWE_BIN:-$ROOT/reverse_firewall_LWE/build/bin}"
R_LWE_BIN="${R_LWE_BIN:-$REMOTE_ROOT/reverse_firewall_LWE/build/bin}"

detect_local_ip() {
  if [[ -n "${LOCAL_IP:-}" ]]; then
    printf '%s\n' "$LOCAL_IP"
    return
  fi
  local detected
  detected="$(ip route get "$REMOTE_IP" 2>/dev/null | awk '{for (i=1; i<=NF; ++i) if ($i=="src") {print $(i+1); exit}}')"
  if [[ -z "$detected" ]]; then
    echo "Could not auto-detect LOCAL_IP for route to $REMOTE_IP; set LOCAL_IP explicitly." >&2
    exit 1
  fi
  printf '%s\n' "$detected"
}

LOCAL_IP="$(detect_local_ip)"
LOCAL_THREADS="${LOCAL_THREADS:-32}"
REMOTE_THREADS="${REMOTE_THREADS:-36}"
LWE_CHUNK="${LWE_CHUNK:-64}"
RF_LWE_PIPELINE="${RF_LWE_PIPELINE:-1}"
RF_LWE_FAST_FIREWALL="${RF_LWE_FAST_FIREWALL:-1}"
TIMEOUT_S="${TIMEOUT_S:-1800}"

if [[ "$#" -gt 0 ]]; then
  SIZES=("$@")
else
  SIZES=(1000)
fi

PROTOCOLS_CSV="${LWE_PROTOCOLS:-direct_batched_ole,rf_batched_ole,rf_opa,rf_psi,direct_ope,rf_ope}"
IFS=',' read -r -a PROTOCOLS <<<"$PROTOCOLS_CSV"

STAMP="$(TZ=UTC date +%Y%m%d_%H%M%S_UTC)"
OUT_DIR="$ROOT/benchmark_2pc_lwe_${STAMP}"
RAW_DIR="$OUT_DIR/raw"
mkdir -p "$RAW_DIR"

CSV="$OUT_DIR/summary.csv"
MD="$OUT_DIR/report.md"
printf 'scheme,protocol,size,status,wall_seconds,local_log,remote_log\n' >"$CSV"

q() {
  printf '%q' "$1"
}

ssh_run() {
  local cmd="$1"
  local quoted
  quoted="$(q "$cmd")"
  ssh "$REMOTE" "bash -lc $quoted"
}

cleanup_ports() {
  local ports=(9101 9102 9103 9201 9202 9203 9301 9302 9303 9311 9312 9313 9320 9401 9402 9403 9501)
  fuser -k "${ports[@]/%//tcp}" >/dev/null 2>&1 || true
  ssh_run "fuser -k ${ports[*]/%//tcp} >/dev/null 2>&1 || true" >/dev/null 2>&1 || true
  sleep 1
}

elapsed_run() {
  local protocol="$1"
  local size="$2"
  local order="$3"
  local local_cmd="$4"
  local remote_cmd="$5"
  local tag="LWE_${protocol}_${size}"
  local local_log="$RAW_DIR/${tag}_local.log"
  local remote_log="$RAW_DIR/${tag}_remote.log"
  local start end elapsed status="ok"
  local local_rc=0
  local remote_rc=0
  local local_pid=0
  local remote_pid=0

  echo "==> LWE / $protocol / $size"
  cleanup_ports
  start="$(date +%s%3N)"

  set +e
  if [[ "$order" == "remote_first" ]]; then
    timeout "$TIMEOUT_S" bash -lc "$(printf 'ssh %q bash -lc %q' "$REMOTE" "$(q "$remote_cmd")")" >"$remote_log" 2>&1 &
    remote_pid=$!
    sleep 1
    timeout "$TIMEOUT_S" bash -lc "$local_cmd" >"$local_log" 2>&1
    local_rc=$?
    wait "$remote_pid"
    remote_rc=$?
  else
    timeout "$TIMEOUT_S" bash -lc "$local_cmd" >"$local_log" 2>&1 &
    local_pid=$!
    sleep 1
    timeout "$TIMEOUT_S" bash -lc "$(printf 'ssh %q bash -lc %q' "$REMOTE" "$(q "$remote_cmd")")" >"$remote_log" 2>&1
    remote_rc=$?
    wait "$local_pid"
    local_rc=$?
  fi
  set -e

  end="$(date +%s%3N)"
  elapsed="$(awk "BEGIN { printf \"%.3f\", ($end - $start) / 1000 }")"
  if [[ "$local_rc" -ne 0 || "$remote_rc" -ne 0 ]]; then
    status="failed(local=$local_rc remote=$remote_rc)"
  fi
  if [[ "$local_rc" -eq 124 || "$remote_rc" -eq 124 ]]; then
    status="timeout(local=$local_rc remote=$remote_rc)"
  fi

  printf 'LWE,%s,%s,%s,%s,%s,%s\n' "$protocol" "$size" "$status" "$elapsed" "$local_log" "$remote_log" >>"$CSV"
  echo "    $status in ${elapsed}s"
  cleanup_ports
}

seq_args() {
  local first="$1"
  local count="$2"
  seq "$first" "$((first + count - 1))" | paste -sd' ' -
}

has_protocol() {
  local needle="$1"
  local p
  for p in "${PROTOCOLS[@]}"; do
    [[ "$p" == "$needle" ]] && return 0
  done
  return 1
}

for n in "${SIZES[@]}"; do
  if has_protocol direct_batched_ole; then
    elapsed_run "direct_batched_ole" "$n" "remote_first" \
      "cd '$LWE_BIN' && OMP_NUM_THREADS=$LOCAL_THREADS RF_LWE_CHUNK=$LWE_CHUNK ./lwe_batch_ole_receiver '$REMOTE_IP' 9501 '$n' 1" \
      "cd '$R_LWE_BIN' && OMP_NUM_THREADS=$REMOTE_THREADS RF_LWE_CHUNK=$LWE_CHUNK ./lwe_batch_ole_sender 9501"
  fi

  if has_protocol rf_batched_ole; then
    elapsed_run "rf_batched_ole" "$n" "local_first" \
      "cd '$LWE_BIN'; export OMP_NUM_THREADS=$LOCAL_THREADS RF_LWE_CHUNK=$LWE_CHUNK RF_LWE_PIPELINE=$RF_LWE_PIPELINE RF_LWE_FAST_FIREWALL=$RF_LWE_FAST_FIREWALL; ./rf_lwe_receiver_firewall 9101 9102 & sleep 1; ./rf_lwe_receiver 127.0.0.1 9101 '$n' 1; wait" \
      "cd '$R_LWE_BIN'; export OMP_NUM_THREADS=$REMOTE_THREADS RF_LWE_CHUNK=$LWE_CHUNK RF_LWE_PIPELINE=$RF_LWE_PIPELINE RF_LWE_FAST_FIREWALL=$RF_LWE_FAST_FIREWALL; ./rf_lwe_sender_firewall 9103 '$LOCAL_IP' 9102 & sleep 1; ./rf_lwe_sender 127.0.0.1 9103; wait"
  fi

  n_oles="$(awk "BEGIN { p=2*$n+1; v=1; while (v<p) v*=2; print v }")"
  if has_protocol rf_opa; then
    elapsed_run "rf_opa" "$n" "local_first" \
      "cd '$LWE_BIN'; export OMP_NUM_THREADS=$LOCAL_THREADS RF_LWE_CHUNK=$LWE_CHUNK RF_LWE_FAST_FIREWALL=$RF_LWE_FAST_FIREWALL; ./opa_receiver_rf 9201 9202 & sleep 1; ./opa_receiver 127.0.0.1 9201 '$n' '$n_oles' 1; wait" \
      "cd '$R_LWE_BIN'; export OMP_NUM_THREADS=$REMOTE_THREADS RF_LWE_CHUNK=$LWE_CHUNK RF_LWE_FAST_FIREWALL=$RF_LWE_FAST_FIREWALL; ./opa_sender_rf 9203 '$LOCAL_IP' 9202 & sleep 1; ./opa_sender 127.0.0.1 9203 '$n'; wait"
  fi

  if has_protocol rf_psi; then
    alice_set="$(seq_args 1 "$n")"
    bob_set="$(seq_args 2 "$n")"
    elapsed_run "rf_psi" "$n" "local_first" \
      "cd '$LWE_BIN'; export OMP_NUM_THREADS=$LOCAL_THREADS RF_LWE_CHUNK=$LWE_CHUNK RF_LWE_FAST_FIREWALL=$RF_LWE_FAST_FIREWALL; ./rf_lwe_receiver_firewall 9301 9302 & ./rf_lwe_sender_firewall 9313 '$REMOTE_IP' 9312 & sleep 1; ./psi_receiver 127.0.0.1 9301 127.0.0.1 9313 '$REMOTE_IP' 9320 '$n' $bob_set; wait" \
      "cd '$R_LWE_BIN'; export OMP_NUM_THREADS=$REMOTE_THREADS RF_LWE_CHUNK=$LWE_CHUNK RF_LWE_FAST_FIREWALL=$RF_LWE_FAST_FIREWALL; ./rf_lwe_receiver_firewall 9311 9312 & ./rf_lwe_sender_firewall 9303 '$LOCAL_IP' 9302 & sleep 1; ./psi_sender 127.0.0.1 9303 127.0.0.1 9311 9320 '$n' $alice_set; wait"
  fi

  lwe_coeffs="$(seq_args 1 $((n + 1)))"
  if has_protocol direct_ope; then
    elapsed_run "direct_ope" "$n" "remote_first" \
      "cd '$LWE_BIN' && OMP_NUM_THREADS=$LOCAL_THREADS RF_LWE_CHUNK=$LWE_CHUNK ./ope_receiver --direct '$REMOTE_IP' 9301 '$n' 7 1" \
      "cd '$R_LWE_BIN' && OMP_NUM_THREADS=$REMOTE_THREADS RF_LWE_CHUNK=$LWE_CHUNK ./ope_sender --direct 9301 '$n' $lwe_coeffs"
  fi

  if has_protocol rf_ope; then
    elapsed_run "rf_ope" "$n" "local_first" \
      "cd '$LWE_BIN'; export OMP_NUM_THREADS=$LOCAL_THREADS RF_LWE_CHUNK=$LWE_CHUNK RF_LWE_FAST_FIREWALL=$RF_LWE_FAST_FIREWALL; ./rf_lwe_receiver_firewall 9401 9402 & sleep 1; ./ope_receiver 127.0.0.1 9401 '$n' 7 1; wait" \
      "cd '$R_LWE_BIN'; export OMP_NUM_THREADS=$REMOTE_THREADS RF_LWE_CHUNK=$LWE_CHUNK RF_LWE_FAST_FIREWALL=$RF_LWE_FAST_FIREWALL; ./rf_lwe_sender_firewall 9403 '$LOCAL_IP' 9402 & sleep 1; ./ope_sender 127.0.0.1 9403 '$n' $lwe_coeffs; wait"
  fi
done

{
  echo "# LWE Two-PC Benchmark"
  echo
  echo "- Timestamp: $STAMP"
  echo "- Local: $LOCAL_IP, $LOCAL_THREADS threads"
  echo "- Remote: $REMOTE_IP ($REMOTE_USER), $REMOTE_THREADS threads"
  echo "- Sizes: ${SIZES[*]}"
  echo "- Protocols: ${PROTOCOLS[*]}"
  echo "- LWE chunk: $LWE_CHUNK"
  echo "- Fast firewall: $RF_LWE_FAST_FIREWALL"
  echo "- Timeout: ${TIMEOUT_S}s per side"
  echo
  echo "| Scheme | Protocol | Size | Status | Wall seconds |"
  echo "|---|---:|---:|---|---:|"
  tail -n +2 "$CSV" | while IFS=, read -r scheme protocol size status seconds local_log remote_log; do
    printf '| %s | %s | %s | %s | %s |\n' "$scheme" "$protocol" "$size" "$status" "$seconds"
  done
} >"$MD"

echo
echo "Summary: $CSV"
echo "Report:  $MD"
