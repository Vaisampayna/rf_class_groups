"""Compare one-sided PSI receiver output with the generated true intersection.

Used only after protocol processes exit; it is not part of measured protocol
time and does not feed data back into either party.
"""

import sys

def check_correctness(true_file, receiver_output_file):
    try:
        from psi_check_io import read_set
        true_set = read_set(true_file)
        receiver_set = read_set(receiver_output_file)
    except (OSError, ValueError) as exc:
        print(f"FAILURE: {exc}")
        sys.exit(1)

    missing = true_set - receiver_set
    extra = receiver_set - true_set
    
    if len(missing) == 0 and len(extra) == 0:
        print(f"SUCCESS: Receiver exactly output the correct intersection of {len(true_set)} elements!")
        sys.exit(0)
    else:
        print("FAILURE: Intersection does not match!")
        if missing:
            print(f"  Missing {len(missing)} elements. Example: {list(missing)[:5]}")
        if extra:
            print(f"  Extra {len(extra)} elements. Example: {list(extra)[:5]}")
        sys.exit(1)

if __name__ == "__main__":
    if len(sys.argv) != 3:
        print("Usage: python3 check_correctness.py <true_intersection_file> <receiver_output_file>")
        sys.exit(1)
        
    check_correctness(sys.argv[1], sys.argv[2])
