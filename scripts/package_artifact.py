#!/usr/bin/env python3
"""Package the source artifact without local builds or experiment outputs."""
import argparse
import hashlib
from pathlib import Path
import tarfile


ROOT = Path(__file__).resolve().parents[1]


def source_files():
    for path in ROOT.iterdir():
        if path.is_file() and (
            path.suffix in {".cpp", ".hpp", ".py", ".sh", ".md"}
            or path.name in {"CMakeLists.txt", "LICENSE", ".gitignore"}
        ) and path.name != "code_review_report.md":
            yield path
    for directory in (ROOT / "third_party", ROOT / "scripts"):
        for path in directory.rglob("*"):
            relative = path.relative_to(directory)
            if path.is_file() and not any(
                part.startswith(".") or part == "__pycache__"
                for part in relative.parts
            ) and path.suffix not in {".pyc", ".log", ".tmp"}:
                yield path
    for name in ("regression.cpp", "regression.py", "check_direct_padding.py"):
        yield ROOT / "review" / name


def archive_metadata(info):
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    return info


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path,
                        default=ROOT / "dist/rf_class_groups_artifact.tar.gz")
    args = parser.parse_args()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    paths = sorted(set(source_files()))
    with tarfile.open(output, "w:gz", dereference=False) as archive:
        for path in paths:
            if path.is_symlink():
                raise ValueError(f"unexpected source symlink: {path}")
            name = Path("rf_class_groups") / path.relative_to(ROOT)
            archive.add(path, arcname=str(name), recursive=False, filter=archive_metadata)
    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    checksum = output.with_name(output.name + ".sha256")
    checksum.write_text(f"{digest}  {output.name}\n")
    print(f"Packaged {len(paths)} source files: {output}")
    print(f"SHA-256: {checksum}")


if __name__ == "__main__":
    main()
