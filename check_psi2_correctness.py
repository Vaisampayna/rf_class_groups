#!/usr/bin/env python3
"""Validate two-way PSI output logs for both parties.

The protocol prints each party's recovered intersection. This checker parses
those logs and compares both outputs with the generated true intersection.
"""

import sys
from pathlib import Path


from psi_check_io import read_set, parse_party_log


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

    try:
        expected = read_set(sys.argv[1])
        got_a = parse_party_log(sys.argv[2])
        got_b = parse_party_log(sys.argv[3])
    except (OSError, ValueError) as exc:
        print(f"[check_psi2] invalid input: {exc}", file=sys.stderr)
        return 1
    ok_a = check_one("Party A", expected, got_a)
    ok_b = check_one("Party B", expected, got_b)
    return 0 if ok_a and ok_b else 1


if __name__ == "__main__":
    raise SystemExit(main())
