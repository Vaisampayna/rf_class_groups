#!/usr/bin/env python3
"""Validate exact two-way RF-PSI output logs for both parties.

The protocol prints each party's recovered intersection. This checker parses
those logs and compares both outputs with the generated true intersection.
"""

import sys
from pathlib import Path


def read_set(path):
    return {line.strip() for line in Path(path).read_text().splitlines() if line.strip()}


def parse_party_log(path):
    values = set()
    in_block = False
    lines = Path(path).read_text(errors="replace").splitlines()
    if not any("intersection:" in line for line in lines):
        return {line.strip() for line in lines if line.strip()}
    for raw in lines:
        line = raw.rstrip()
        if "intersection:" in line:
            in_block = True
            continue
        if in_block and "total intersection size:" in line:
            break
        if not in_block:
            continue
        item = line.strip()
        if not item or item == "(empty)" or item.startswith("..."):
            continue
        values.add(item)
    return values


def check_one(label, expected, got):
    missing = expected - got
    extra = got - expected
    if not missing and not extra:
        print(f"[check_psi2] {label}: SUCCESS ({len(expected)} elements)")
        return True
    print(f"[check_psi2] {label}: FAILURE")
    if missing:
        print(f"  Missing {len(missing)} elements. Example: {list(missing)[:5]}")
    if extra:
        print(f"  Extra {len(extra)} elements. Example: {list(extra)[:5]}")
    return False


def main():
    if len(sys.argv) != 4:
        print(
            "Usage: check_psi2_correctness.py <true_intersection.txt> <party_A.log> <party_B.log>",
            file=sys.stderr,
        )
        return 2

    expected = read_set(sys.argv[1])
    got_a = parse_party_log(sys.argv[2])
    got_b = parse_party_log(sys.argv[3])
    ok_a = check_one("Party A", expected, got_a)
    ok_b = check_one("Party B", expected, got_b)
    return 0 if ok_a and ok_b else 1


if __name__ == "__main__":
    raise SystemExit(main())
