"""Completion checks shared by timing recovery and publication tables."""
import csv
import math
from pathlib import Path

def valid_ms(value):
    try:
        value = float(value)
    except (TypeError, ValueError):
        return None
    return value if math.isfinite(value) and value >= 0 else None

def run_succeeded(root, run, require_check=False):
    path = Path(root) / "runs.csv"
    if not path.exists():
        return False
    with path.open(newline="") as stream:
        matches = [row for row in csv.DictReader(stream) if row.get("name") == run]
    if not matches or any(matches[-1].get(key) != "0" for key in ("local_rc", "remote_rc")):
        return False
    if require_check:
        path = Path(root) / "checks.csv"
        if not path.exists():
            return False
        with path.open(newline="") as stream:
            checks = [row for row in csv.DictReader(stream) if row.get("name") == run]
        # An explicit timing-only run may opt out; absence is not an opt-out.
        if not checks or checks[-1].get("checker_rc") not in ("0", "skipped"):
            return False
    return True
