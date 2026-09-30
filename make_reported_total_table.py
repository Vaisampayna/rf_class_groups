#!/usr/bin/env python3
"""Print the total-time table from benchmark logs.

For most binaries, protocol_times.csv already contains the desired per-party
wall-clock protocol time.  For RF-OLE/RF-OPA based wrappers with narrower
subphase timing files, this script reconstructs full wrapper-call timings from
the raw logs:

  total protocol time = max(local party time, remote party time).

Input generation and offline checker dumps remain excluded.
"""

import csv
import re
import sys
from pathlib import Path

SIZES = [1024, 2048, 4096, 8192, 16384, 32768]
NUM = r"([0-9.eE+-]+)"


def last_number(path: Path, pattern: str):
    if not path.exists():
        return None
    vals = re.findall(pattern, path.read_text(errors="replace"))
    return float(vals[-1]) if vals else None


def csv_party_times(root: Path, run: str):
    out = {}
    p = root / "protocol_times.csv"
    if not p.exists():
        return out
    with p.open(newline="") as f:
        for row in csv.DictReader(f):
            if row["run"] == run:
                out[row["side"]] = float(row["protocol_time_ms"])
    return out


def max_csv(root: Path, run: str):
    vals = csv_party_times(root, run).values()
    return max(vals) if vals else None


def max_from_logs(root: Path, run: str, pattern: str):
    vals = []
    for side in ("local", "remote"):
        v = last_number(root / "raw" / f"{run}_{side}.log", pattern)
        if v is not None:
            vals.append(v)
    return max(vals) if vals else None


def rf_opa_wall(root: Path, run: str):
    vals = []
    for side in ("local", "remote"):
        log = root / "raw" / f"{run}_{side}.log"
        eval_ms = last_number(log, rf"OPA setup/evaluation done in {NUM} ms") or 0.0
        exch_ms = last_number(
            log,
            rf"OPA RF-OLE (?:output-share receive|send-share exchange) done in {NUM} ms",
        )
        if exch_ms is not None:
            vals.append(eval_ms + exch_ms)
    return max(vals) if vals else None


def rf_psi_wall(root: Path, run: str):
    vals = []
    for side in ("local", "remote"):
        log = root / "raw" / f"{run}_{side}.log"
        if side == "local":
            poly_ms = last_number(log, rf"vanishing polynomial build done in {NUM} ms") or 0.0
            rfopa_ms = last_number(log, rf"RF-OPA output-share receive done in {NUM} ms")
            out_ms = last_number(log, rf"output reconstruction and verification done in {NUM} ms")
            if rfopa_ms is not None and out_ms is not None:
                vals.append(poly_ms + rfopa_ms + out_ms)
        else:
            poly_ms = (
                last_number(
                    log,
                    rf"vanishing polynomial and random mask polynomials generation done in {NUM} ms",
                )
                or 0.0
            )
            rfopa_ms = last_number(log, rf"RF-OPA send-share exchange done in {NUM} ms")
            if rfopa_ms is not None:
                vals.append(poly_ms + rfopa_ms)
    return max(vals) if vals else None


def rf_psi2_wall(root: Path, n: int):
    csv_val = max_csv(root, f"rf_psi2_reveal_{n}")
    if csv_val is not None:
        return csv_val
    child = root / f"run_rf_psi2_{n}" / "raw"
    vals = []
    for side, label in (("local", "B"), ("remote", "A")):
        log = child / f"rf_psi2_reveal_{n}_{side}.log"
        reveal_ms = last_number(log, rf"reveal-back phase done in {NUM} ms") or 0.0
        if label == "B":
            poly_ms = last_number(log, rf"vanishing polynomial build done in {NUM} ms") or 0.0
            rfopa_ms = last_number(log, rf"RF-OPA output-share receive done in {NUM} ms")
            out_ms = last_number(log, rf"output reconstruction and verification done in {NUM} ms")
            if rfopa_ms is not None and out_ms is not None:
                vals.append(poly_ms + rfopa_ms + out_ms + reveal_ms)
        else:
            poly_ms = (
                last_number(
                    log,
                    rf"vanishing polynomial and random mask polynomials generation done in {NUM} ms",
                )
                or 0.0
            )
            rfopa_ms = last_number(log, rf"RF-OPA send-share exchange done in {NUM} ms")
            if rfopa_ms is not None:
                vals.append(poly_ms + rfopa_ms + reveal_ms)
    return max(vals) if vals else None


def sec(ms):
    return None if ms is None else ms / 1000.0


def first_value(*vals):
    for v in vals:
        if v is not None:
            return v
    return None


def fmt_row(name, vals):
    cells = ["--" if v is None else f"{v:.2f}" for v in vals]
    return rf"\hspace{{2mm}} {name}" + "\n  & " + " & ".join(cells) + r" \\"


def main():
    if len(sys.argv) not in (2, 4):
        print(
            "Usage: make_reported_total_table.py <combined_suite_dir>\n"
            "   or: make_reported_total_table.py <cg_sweep_dir> <ole3_sweep_dir> <psi_fast_sweep_dir>",
            file=sys.stderr,
        )
        return 2
    if len(sys.argv) == 2:
        cg_root = ole3_root = psi_root = Path(sys.argv[1])
    else:
        cg_root = Path(sys.argv[1])
        ole3_root = Path(sys.argv[2])
        psi_root = Path(sys.argv[3])

    rows = {
        "Batched OLE": [sec(max_csv(cg_root, f"direct_batched_ole_{n}")) for n in SIZES],
        "Batched RF OLE": [
            sec(first_value(
                max_csv(cg_root, f"rf_batched_ole_{n}"),
                max_from_logs(
                    cg_root,
                    f"rf_batched_ole_{n}",
                    rf"RF-OLE protocol (?:receive/decrypt|send/compute) phase done in {NUM} ms",
                ),
            ))
            for n in SIZES
        ],
        "3-Round OLE": [sec(max_csv(ole3_root, f"direct_ole3_{n}")) for n in SIZES],
        "3-Round RF OLE": [sec(max_csv(ole3_root, f"rf_ole3_{n}")) for n in SIZES],
        "OPE": [sec(max_csv(cg_root, f"direct_ope_{n}")) for n in SIZES],
        "RF OPE": [sec(max_csv(cg_root, f"rf_ope_{n}")) for n in SIZES],
        "OPA": [sec(max_csv(cg_root, f"direct_opa_{n}")) for n in SIZES],
        "RF OPA": [
            sec(first_value(max_csv(cg_root, f"rf_opa_{n}"), rf_opa_wall(cg_root, f"rf_opa_{n}")))
            for n in SIZES
        ],
        "PSI": [sec(max_csv(psi_root, f"direct_psi_{n}")) for n in SIZES],
        "RF PSI (one-way)": [
            sec(first_value(max_csv(psi_root, f"rf_psi_{n}"), rf_psi_wall(psi_root, f"rf_psi_{n}")))
            for n in SIZES
        ],
        "PSI (two-way)": [sec(max_csv(psi_root, f"direct_psi2_{n}")) for n in SIZES],
        "RF PSI (two-way)": [sec(rf_psi2_wall(psi_root, n)) for n in SIZES],
    }

    for name, vals in rows.items():
        print(fmt_row(name, vals))
        print(r"\hline")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
