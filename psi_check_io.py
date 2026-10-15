"""Strict numeric PSI checker inputs. Full output files are preferred to logs."""
import re
from pathlib import Path

def numeric_set(tokens, label):
    values = set()
    for token in tokens:
        if not re.fullmatch(r"[0-9]+", token):
            raise ValueError(f"{label}: invalid nonnegative decimal element {token!r}")
        value = int(token)
        if value in values:
            raise ValueError(f"{label}: duplicate element {value}")
        values.add(value)
    return values

def read_set(path):
    return numeric_set(Path(path).read_text().split(), str(path))

def parse_party_log(path):
    lines = Path(path).read_text().splitlines()
    starts = [i for i, line in enumerate(lines) if "intersection:" in line]
    if not starts:
        return read_set(path)
    if len(starts) != 1:
        raise ValueError(f"{path}: ambiguous intersection blocks")
    tokens = []
    for line in lines[starts[0] + 1:]:
        match = re.search(r"total intersection size:\s*(\d+)\s*$", line)
        if match:
            values = numeric_set(tokens, str(path))
            if len(values) != int(match.group(1)):
                raise ValueError(f"{path}: declared intersection size differs from complete output")
            return values
        item = line.strip()
        if item.startswith("..."):
            raise ValueError(f"{path}: truncated log; supply the complete intersection output file")
        if item and item != "(empty)":
            tokens.append(item)
    raise ValueError(f"{path}: missing intersection completion summary")
