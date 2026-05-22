#!/usr/bin/env python3
"""Extract structured timing rows from protocol stderr logs.

The benchmark sweep keeps raw logs for reproducibility and appends parsed
phase timings to timings.csv for easier plotting/debugging.
"""

import csv
import re
import sys
from pathlib import Path


DONE_RE = re.compile(r"^\[(?P<component>[^\]]+)\]\s+(?P<metric>.+?)\s+done in\s+(?P<ms>[0-9.]+)\s+ms")
OPTIME_RE = re.compile(
    r"^\[(?P<component>[^\]]+)\]\s+\[OPTIME\]\s+(?P<metric>.+?)\s+sum=(?P<sum>[0-9.]+)\s+ms,\s+avg=(?P<avg>[0-9.]+)\s+ms/op"
)
INIT_RE = re.compile(r"^(?P<metric>Init)\s+done in\s+(?P<ms>[0-9.]+)\s+ms")


def emit_rows(run_name, side, log_path):
    path = Path(log_path)
    if not path.exists():
        return []

    rows = []
    for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        match = DONE_RE.search(line)
        if match:
            rows.append({
                "run": run_name,
                "side": side,
                "component": match.group("component"),
                "metric": match.group("metric"),
                "value_ms": match.group("ms"),
                "value_kind": "elapsed",
                "log": str(path),
                "line": line_no,
            })
            continue

        match = OPTIME_RE.search(line)
        if match:
            rows.append({
                "run": run_name,
                "side": side,
                "component": match.group("component"),
                "metric": match.group("metric") + " sum",
                "value_ms": match.group("sum"),
                "value_kind": "op_sum",
                "log": str(path),
                "line": line_no,
            })
            rows.append({
                "run": run_name,
                "side": side,
                "component": match.group("component"),
                "metric": match.group("metric") + " avg",
                "value_ms": match.group("avg"),
                "value_kind": "op_avg",
                "log": str(path),
                "line": line_no,
            })
            continue

        match = INIT_RE.search(line)
        if match:
            rows.append({
                "run": run_name,
                "side": side,
                "component": "process",
                "metric": match.group("metric"),
                "value_ms": match.group("ms"),
                "value_kind": "elapsed",
                "log": str(path),
                "line": line_no,
            })

    return rows


def main():
    if len(sys.argv) != 5:
        print("Usage: extract_timings.py <run_name> <side> <log_path> <timings_csv>", file=sys.stderr)
        return 2

    run_name, side, log_path, out_csv = sys.argv[1:]
    rows = emit_rows(run_name, side, log_path)
    if not rows:
        return 0

    out = Path(out_csv)
    out.parent.mkdir(parents=True, exist_ok=True)
    write_header = not out.exists() or out.stat().st_size == 0
    with out.open("a", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["run", "side", "component", "metric", "value_ms", "value_kind", "log", "line"],
        )
        if write_header:
            writer.writeheader()
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
