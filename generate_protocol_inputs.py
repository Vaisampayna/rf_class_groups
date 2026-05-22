#!/usr/bin/env python3
"""Generate random input files for standalone two-party protocol launchers.

The generated files are prepared before protocol clocks start, so input
sampling and modular reduction are excluded from reported protocol time.
"""

import argparse
import random
import secrets
from pathlib import Path


def values(count, bits, seed):
    rng = random.Random(seed) if seed is not None else secrets.SystemRandom()
    for _ in range(count):
        yield str(rng.getrandbits(bits))


def write_lines(path, lines):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Generate random benchmark input files below the plaintext modulus."
    )
    parser.add_argument("mode", choices=["list", "ole-sender"])
    parser.add_argument("count", type=int)
    parser.add_argument("out")
    parser.add_argument("--bits", type=int, default=128)
    parser.add_argument("--seed", type=int, default=None)
    args = parser.parse_args()

    if args.mode == "list":
        write_lines(args.out, values(args.count, args.bits, args.seed))
    else:
        a_vals = list(values(args.count, args.bits, args.seed))
        b_seed = None if args.seed is None else args.seed + 1
        b_vals = list(values(args.count, args.bits, b_seed))
        write_lines(args.out, [f"{a} {b}" for a, b in zip(a_vals, b_vals)])


if __name__ == "__main__":
    main()
