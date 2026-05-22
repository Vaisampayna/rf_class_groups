"""Generate PSI set files with a known intersection.

Both machines run this with the same seed so sender/receiver set files and the
true_intersection.txt checker file agree without exchanging private inputs.
"""

import random
import sys

DEFAULT_BITS = 128

def sample_unique(rng, count, used, bound):
    values = []
    while len(values) < count:
        value = str(rng.randrange(bound))
        if value in used:
            continue
        used.add(value)
        values.append(value)
    return values

def generate_sets(size_a, size_b, overlap, file_a, file_b, file_true,
                  seed=42, bits=DEFAULT_BITS, field_bound=None):
    if overlap > min(size_a, size_b):
        raise ValueError("Overlap cannot be greater than the size of the smallest set.")

    rng = random.Random(seed)
    bit_bound = 1 << bits
    bound = min(bit_bound, field_bound) if field_bound is not None else bit_bound
    if bound <= size_a + size_b:
        raise ValueError("Sampling bound is too small for the requested unique sets.")
    used = set()
    intersection = sample_unique(rng, overlap, used, bound)
    only_a = sample_unique(rng, size_a - overlap, used, bound)
    only_b = sample_unique(rng, size_b - overlap, used, bound)

    set_a = intersection + only_a
    set_b = intersection + only_b

    rng.shuffle(set_a)
    rng.shuffle(set_b)

    with open(file_a, 'w') as fa:
        fa.write("\n".join(set_a) + "\n")

    with open(file_b, 'w') as fb:
        fb.write("\n".join(set_b) + "\n")

    with open(file_true, 'w') as fi:
        fi.write("\n".join(intersection) + "\n")

    if field_bound is None:
        print(f"Generated {file_a} (|S_A|={size_a}) and {file_b} (|S_B|={size_b}) with {overlap} overlapping {bits}-bit plaintext elements.")
    else:
        print(f"Generated {file_a} (|S_A|={size_a}) and {file_b} (|S_B|={size_b}) with {overlap} overlapping elements below min(2^{bits}, q).")

if __name__ == "__main__":
    if len(sys.argv) not in (7, 8, 9, 10):
        print("Usage: python3 generate_sets.py <size_A> <size_B> <overlap> <out_file_A> <out_file_B> <true_intersection_out> [seed] [bits] [field_bound_q]")
        sys.exit(1)

    seed_arg = int(sys.argv[7]) if len(sys.argv) >= 8 else 42
    bits_arg = int(sys.argv[8]) if len(sys.argv) >= 9 else DEFAULT_BITS
    field_bound_arg = int(sys.argv[9]) if len(sys.argv) >= 10 else None
    generate_sets(int(sys.argv[1]), int(sys.argv[2]), int(sys.argv[3]),
                  sys.argv[4], sys.argv[5], sys.argv[6],
                  seed_arg, bits_arg, field_bound_arg)
