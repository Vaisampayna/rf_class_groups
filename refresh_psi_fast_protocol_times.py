#!/usr/bin/env python3
"""Regenerate protocol_times.csv for a timing-only PSI fast sweep.

The fast sweep can be interrupted or queried while children are still running.
This helper reconstructs the current party timing rows from completed raw logs.
"""

import csv
import re
import sys
from pathlib import Path

PROTO_RE = re.compile(
    r"\[(?P<component>[^\]]+)\]\s+protocol end-to-end excluding input sampling done in\s+"
    r"(?P<ms>[0-9.eE+-]+)\s+ms"
)
REVEAL_RE = re.compile(r"reveal-back phase done in\s+(?P<ms>[0-9.eE+-]+)\s+ms")


def add(rows, seen, run, side, component, ms, source):
    key = (run, side, component, source)
    if key in seen:
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
            one_way = float(match.group("ms"))
        match = REVEAL_RE.search(line)
        if match and reveal is None:
            reveal = float(match.group("ms"))
    if one_way is None:
        return None
    return one_way + (reveal or 0.0)


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
            if run.startswith("rf_psi2_reveal_"):
                ms = parse_psi2_log(log)
                if ms is not None:
                    component = "psi2_B" if side == "local" else "psi2_A"
                    add(rows, seen, run, side, component, f"{ms:.6f}", str(log))
            else:
                parsed = parse_party_log(log)
                if parsed:
                    add(rows, seen, run, side, parsed[0], parsed[1], str(log))

    for child in sorted(root.glob("run_rf_psi2_*")):
        run = child.name.replace("run_rf_psi2_", "rf_psi2_reveal_")
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

    with out.open("w", newline="") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["run", "side", "component", "protocol_time_ms", "source"],
        )
        writer.writeheader()
        writer.writerows(rows)

    print(f"wrote {len(rows)} rows to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
