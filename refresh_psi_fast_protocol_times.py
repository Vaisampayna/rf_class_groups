#!/usr/bin/env python3
"""Regenerate protocol_times.csv for a timing-only PSI fast sweep.

The fast sweep can be interrupted or queried while children are still running.
This helper reconstructs the current party timing rows from completed raw logs.
"""

import csv
import re
import sys
import os
import tempfile
from benchmark_status import valid_ms, run_succeeded
from pathlib import Path

PROTO_RE = re.compile(
    r"\[(?P<component>[^\]]+)\]\s+protocol end-to-end excluding input sampling done in\s+"
    r"(?P<ms>[0-9.eE+-]+)\s+ms"
)
REVEAL_RE = re.compile(r"reveal-back phase done in\s+(?P<ms>[0-9.eE+-]+)\s+ms")


def add(rows, seen, run, side, component, ms, source):
    key = (run, side, component, source)
    if valid_ms(ms) is None or side not in ("local", "remote") or key in seen:
        return
    seen.add(key)
    rows.append(
        {
            "run": run,
            "side": side,
            "component": component,
            "protocol_time_ms": ms,
            "source": source,
        }
    )


def parse_party_log(path):
    last = None
    for line in path.read_text(errors="replace").splitlines():
        match = PROTO_RE.search(line)
        if match:
            last = (match.group("component"), match.group("ms"))
    return last


def parse_psi2_log(path):
    one_way = None
    reveal = None
    for line in path.read_text(errors="replace").splitlines():
        match = PROTO_RE.search(line)
        if match:
            one_way = valid_ms(match.group("ms"))
        match = REVEAL_RE.search(line)
        if match and reveal is None:
            reveal = valid_ms(match.group("ms"))
    if valid_ms(one_way) is None or valid_ms(reveal) is None:
        return None
    return one_way + reveal


def main():
    if len(sys.argv) != 2:
        print("Usage: refresh_psi_fast_protocol_times.py <sweep_dir>", file=sys.stderr)
        return 2

    root = Path(sys.argv[1])
    out = root / "protocol_times.csv"
    rows = []
    seen = set()

    raw_dir = root / "raw"
    if raw_dir.exists():
        for log in sorted(raw_dir.glob("*.log")):
            name = log.name
            if name.endswith("_local.log"):
                side = "local"
                run = name[:-10]
            elif name.endswith("_remote.log"):
                side = "remote"
                run = name[:-11]
            else:
                continue
            if not run_succeeded(root, run):
                continue
            if run.startswith("rf_psi2_reveal_"):
                ms = parse_psi2_log(log)
                if ms is not None:
                    component = "psi2_B" if side == "local" else "psi2_A"
                    add(rows, seen, run, side, component, f"{ms:.6f}", str(log))
            else:
                parsed = parse_party_log(log)
                if parsed:
                    add(rows, seen, run, side, parsed[0], parsed[1], str(log))

    children = set(root.glob("run_rf_psi2_*")) | set(root.glob("run_*")) | set(root.glob("rf_psi2_*"))
    for child in sorted(children):
        if not child.is_dir() or not (child / "raw").exists():
            continue
        # The actual run names are supplied by controller status records.
        status = child / "runs.csv"
        if not status.exists():
            continue
        with status.open(newline="") as stream:
            run_names = [row.get("name", "") for row in csv.DictReader(stream)
                         if row.get("name", "").startswith("rf_psi2_reveal_")]
        if len(set(run_names)) != 1:
            continue
        run = run_names[-1]
        if not run_succeeded(root, run) and not run_succeeded(child, run):
            continue
        for side, suffix, component in (
            ("local", "local", "psi2_B"),
            ("remote", "remote", "psi2_A"),
        ):
            logs = list((child / "raw").glob(f"*_{suffix}.log"))
            if not logs:
                continue
            ms = parse_psi2_log(logs[0])
            if ms is not None:
                add(rows, seen, run, side, component, f"{ms:.6f}", str(logs[0]))

    # Retain valid party-clock rows already collected from complete runs.
    if out.exists():
        with out.open(newline="") as stream:
            for row in csv.DictReader(stream):
                if run_succeeded(root, row.get("run")) and valid_ms(row.get("protocol_time_ms")) is not None:
                    key = (row["run"], row["side"])
                    rows = [existing for existing in rows if (existing["run"], existing["side"]) != key]
                    rows.append(row)
    # Publish only complete pairs. A single party remains partial progress.
    complete = {row["run"] for row in rows
                if {r["side"] for r in rows if r["run"] == row["run"]} == {"local", "remote"}}
    rows = [row for row in rows if row["run"] in complete]
    with tempfile.NamedTemporaryFile("w", newline="", dir=root, delete=False) as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["run", "side", "component", "protocol_time_ms", "source"],
        )
        writer.writeheader()
        writer.writerows(rows)
        temp_name = f.name
    os.replace(temp_name, out)

    print(f"wrote {len(rows)} rows to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
