"""Targeted direct PSI2 padding checks over plain TCP."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from psi_check_io import read_set
from regression import free_ports

def main():
    build = Path(sys.argv[1] if len(sys.argv) > 1 else "build").resolve()
    cases = [
        ("no-zero", [1, 3, 4], [1, 2, 3], {1, 3}, {1, 3}),
        ("zero-padding-collision", [0, 1, 3], [1, 2, 3], {0, 1, 3}, {1, 3}),
        ("zero-is-real", [0, 1, 3], [0, 2, 3], {0, 3}, {0, 3}),
        ("no-empty-slots", [0, 1, 3], [0, 1, 3], {0, 1, 3}, {0, 1, 3}),
    ]
    with tempfile.TemporaryDirectory(prefix="cg-direct-padding-") as directory:
        root = Path(directory)
        for name, a, b, expected_a, expected_b in cases:
            path = root / name
            path.mkdir()
            (path / "a").write_text("\n".join(map(str, a)) + "\n")
            (path / "b").write_text("\n".join(map(str, b)) + "\n")
            port, reveal = free_ports(2)
            env = dict(os.environ, CG_BIND_ADDR="127.0.0.1",
                       CG_PROTOCOL_ID="direct-psi2",
                       CG_RF_THREADS="2", CG_RF_LANES="2", CG_RF_CHUNK_SIZE="2",
                       CG_IO_TIMEOUT_S="10", CG_CONNECT_RETRIES="100",
                       CG_FIXED_Q="170141183460469232709364739622490341377")
            commands = [
                [build / "cg_psi2_receiver", "127.0.0.1", len(a), "--input-file", path / "b",
                 "--output-file", path / "out-b", "--port", port, "--reveal-port", reveal],
                [build / "cg_psi2_sender", "127.0.0.1", len(b), "--input-file", path / "a",
                 "--output-file", path / "out-a", "--port", port, "--reveal-port", reveal],
            ]
            processes, streams = [], []
            try:
                for i, command in enumerate(commands):
                    stream = (path / f"{i}.log").open("w")
                    streams.append(stream)
                    processes.append(subprocess.Popen(list(map(str, command)), env=env,
                                                      stdout=stream, stderr=stream))
                deadline = time.monotonic() + 30
                while any(p.poll() is None for p in processes):
                    assert time.monotonic() < deadline, "protocol timed out"
                    assert all(p.poll() in (None, 0) for p in processes), "protocol failed"
                    time.sleep(0.05)
                assert all(p.returncode == 0 for p in processes)
                assert read_set(path / "out-a") == expected_a
                assert read_set(path / "out-b") == expected_b
            except Exception:
                for stream in streams: stream.flush()
                for log in path.glob("*.log"): print(log.read_text(), file=sys.stderr)
                raise
            finally:
                for p in processes:
                    if p.poll() is None: p.terminate()
                for p in processes:
                    try: p.wait(timeout=2)
                    except subprocess.TimeoutExpired: p.kill(); p.wait()
                for stream in streams: stream.close()
            print(f"PASS {name}", flush=True)

if __name__ == "__main__":
    main()
