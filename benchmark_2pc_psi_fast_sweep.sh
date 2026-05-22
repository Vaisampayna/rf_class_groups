#!/usr/bin/env bash
set -euo pipefail

# Fast timing-only sweep for direct PSI, direct two-way PSI, one-way RF-PSI,
# and the current two-way RF-PSI reveal construction.  Correctness checks are
# intentionally skipped so urgent timing rows can be collected first.

source "$(cd "$(dirname "$0")" && pwd)/scripts/2pc_common.sh"

SIZES=("$@")
if [[ "$#" -eq 0 ]]; then
    SIZES=(1024 2048 4096 8192 16384 32768)
fi

init_run_dir "benchmark_2pc_psi_fast_sweep"

ENV_LOCAL="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_LOCAL"
ENV_REMOTE="$(common_env) CG_RF_THREADS=$CG_RF_THREADS_REMOTE"
FAILED=0

refresh_protocol_times() {
    python3 "$SCRIPT_ROOT/refresh_psi_fast_protocol_times.py" "$OUT_DIR" || true
}

merge_child_csvs() {
    local child="$1"
    local row

    if [[ -f "$child/protocol_times.csv" ]]; then
        while IFS= read -r row; do
            [[ "$row" == run,* || -z "$row" ]] && continue
            printf '%s\n' "$row" >>"$OUT_DIR/protocol_times.csv"
        done <"$child/protocol_times.csv"
    fi
    if [[ -f "$child/runs.csv" ]]; then
        tail -n +2 "$child/runs.csv" >>"$OUT_DIR/runs.csv"
    fi
    if [[ -f "$child/timings.csv" ]]; then
        tail -n +2 "$child/timings.csv" >>"$OUT_DIR/timings.csv"
    fi
}

append_protocol_times_from_timings() {
    local timings_csv="$1"
    [[ -f "$timings_csv" ]] || return 0
    python3 - "$timings_csv" "$OUT_DIR/protocol_times.csv" <<'PY'
import csv
import sys

timings_path, out_path = sys.argv[1], sys.argv[2]
seen = set()
try:
    with open(out_path, newline="") as f:
        for row in csv.DictReader(f):
            seen.add((row["run"], row["side"], row["component"], row["source"]))
except FileNotFoundError:
    pass

rows = []
with open(timings_path, newline="") as f:
    for row in csv.DictReader(f):
        if row.get("metric") != "protocol end-to-end excluding input sampling":
            continue
        key = (row["run"], row["side"], row["component"], row["log"])
        if key in seen:
            continue
        seen.add(key)
        rows.append({
            "run": row["run"],
            "side": row["side"],
            "component": row["component"],
            "protocol_time_ms": row["value_ms"],
            "source": row["log"],
        })

if rows:
    with open(out_path, "a", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["run", "side", "component", "protocol_time_ms", "source"],
        )
        writer.writerows(rows)
PY
}

for N in "${SIZES[@]}"; do
    set +e
    run_pair "direct_psi_${N}" "local_first" \
        "cd '$LOCAL_ROOT/$BUILD_DIR' && export $ENV_LOCAL; ./cg_psi_receiver '$N' --random '$N' 42 --port 9103" \
        "cd '$REMOTE_ROOT/$BUILD_DIR' && export $ENV_REMOTE; ./cg_psi_sender '$LOCAL_IP' '$N' --random '$N' 42 --port 9103"
    rc=$?
    set -e
    if (( rc != 0 )); then
        FAILED=1
        echo "WARNING: direct_psi_${N} failed with rc=${rc}; continuing." >&2
    fi
    append_protocol_times_from_timings "$OUT_DIR/timings.csv"
    refresh_protocol_times

    child="$OUT_DIR/run_direct_psi2_${N}"
    set +e
    CG_SKIP_CHECKS=1 OUT_DIR="$child" bash "$SCRIPT_ROOT/run_2pc_direct_psi2.sh" "$N"
    rc=$?
    set -e
    merge_child_csvs "$child"
    append_protocol_times_from_timings "$child/timings.csv"
    if (( rc != 0 )); then
        FAILED=1
        echo "WARNING: direct_psi2_${N} failed with rc=${rc}; merged available artifacts and continuing." >&2
    fi
    refresh_protocol_times

    set +e
    run_pair "rf_psi_${N}" "local_first" \
        "cd '$LOCAL_ROOT' && CG_BUILD_DIR='$LOCAL_ROOT/$BUILD_DIR' $ENV_LOCAL bash ./run_rf_cg_psi_lan_receiver.sh --random '$N' '$N' 42" \
        "cd '$REMOTE_ROOT' && CG_BUILD_DIR='$REMOTE_ROOT/$BUILD_DIR' $ENV_REMOTE bash ./run_rf_cg_psi_lan_sender.sh '$LOCAL_IP' --random '$N' '$N' 42"
    rc=$?
    set -e
    if (( rc != 0 )); then
        FAILED=1
        echo "WARNING: rf_psi_${N} failed with rc=${rc}; continuing." >&2
    fi
    append_protocol_times_from_timings "$OUT_DIR/timings.csv"
    refresh_protocol_times

    child="$OUT_DIR/run_rf_psi2_${N}"
    set +e
    CG_SKIP_CHECKS=1 OUT_DIR="$child" bash "$SCRIPT_ROOT/run_2pc_rf_psi2.sh" "$N"
    rc=$?
    set -e
    merge_child_csvs "$child"
    append_protocol_times_from_timings "$child/timings.csv"
    if (( rc != 0 )); then
        FAILED=1
        echo "WARNING: rf_psi2_reveal_${N} failed with rc=${rc}; merged available artifacts and continuing." >&2
    fi
    refresh_protocol_times
done

refresh_protocol_times
finish_run
echo "Fast PSI timings: $OUT_DIR/protocol_times.csv"
exit "$FAILED"
