import argparse
import ast
import csv
import importlib.util
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from psi_check_io import read_set, parse_party_log
from refresh_psi_fast_protocol_times import parse_psi2_log
from make_reported_total_table import max_csv, max_from_logs
from generate_sets import generate_sets

def execute(args, *, env=None, timeout=45, expected=0):
    result = subprocess.run(list(map(str, args)), capture_output=True, text=True,
                            env=env, timeout=timeout)
    assert result.returncode == expected, (args, result.returncode, result.stdout, result.stderr)
    return result

def rejected(fn):
    try:
        fn()
    except (ValueError, OSError):
        return
    raise AssertionError("invalid input was accepted")

def python_checks(tmp):
    file = tmp / "set.txt"
    file.write_text("1\n1\n")
    rejected(lambda: read_set(file))
    file.write_text("01\n")
    assert read_set(file) == {1}
    file.write_text("-1\n")
    rejected(lambda: read_set(file))
    log = tmp / "party.log"
    log.write_text("[psi2_A] intersection:\n  1\n  ... (showing first 20 only on stdout)\n"
                   "[psi2_A] total intersection size: 21\n")
    rejected(lambda: parse_party_log(log))
    log.write_text("[psi2_A] intersection:\n  1\n[psi2_A] total intersection size: 2\n")
    rejected(lambda: parse_party_log(log))
    log.write_text("[psi2_A] intersection:\n  (empty)\n[psi2_A] total intersection size: 0\n")
    assert parse_party_log(log) == set()
    prefix = "[psi_receiver] protocol end-to-end excluding input sampling done in 100 ms\n"
    log.write_text(prefix)
    assert parse_psi2_log(log) is None
    log.write_text(prefix + "[psi2_reveal_receiver] reveal-back phase done in 25 ms\n")
    assert parse_psi2_log(log) == 125
    log.write_text(prefix + "reveal-back phase done in -25 ms\n")
    assert parse_psi2_log(log) is None
    (tmp / "runs.csv").write_text("name,local_rc,remote_rc\ntest,0,0\n")
    (tmp / "checks.csv").write_text("name,checker_rc\ntest,0\n")
    header = "run,side,component,protocol_time_ms,source\n"
    (tmp / "protocol_times.csv").write_text(header + "test,local,A,100,log\n")
    assert max_csv(tmp, "test") is None
    (tmp / "protocol_times.csv").write_text(header + "test,local,A,100,log\ntest,remote,B,150,log\n")
    assert max_csv(tmp, "test") == 150
    (tmp / "checks.csv").write_text("name,checker_rc\ntest,1\n")
    assert max_csv(tmp, "test") is None
    (tmp / "checks.csv").write_text("name,checker_rc\ntest,skipped\n")
    assert max_csv(tmp, "test") == 150
    (tmp / "runs.csv").write_text("name,local_rc,remote_rc\ntest,0,1\n")
    assert max_csv(tmp, "test") is None
    # Recover only successful, complete pairs and preserve complete party clocks.
    recovery = tmp / "recovery"
    (recovery / "raw").mkdir(parents=True)
    run = "rf_psi2_reveal_4"
    (recovery / "runs.csv").write_text(f"name,local_rc,remote_rc\n{run},0,0\n")
    for side in ("local", "remote"):
        (recovery / "raw" / f"{run}_{side}.log").write_text(prefix)
    execute([sys.executable, ROOT / "refresh_psi_fast_protocol_times.py", recovery])
    assert len((recovery / "protocol_times.csv").read_text().splitlines()) == 1
    for side in ("local", "remote"):
        (recovery / "raw" / f"{run}_{side}.log").write_text(prefix + "reveal-back phase done in 25 ms\n")
    execute([sys.executable, ROOT / "refresh_psi_fast_protocol_times.py", recovery])
    assert len((recovery / "protocol_times.csv").read_text().splitlines()) == 3
    q = 170141183460469232709364739622490341377
    generate_sets(5, 5, 1, tmp / "A", tmp / "B", tmp / "I", seed=42, bits=128, field_bound=q)
    assert max(read_set(tmp / "A") | read_set(tmp / "B")) < q
    rejected(lambda: generate_sets(-1, 1, 0, tmp / "A", tmp / "B", tmp / "I"))
    env = dict(os.environ, LOCAL_IP="unused", REMOTE_IP="unused", LOCAL_USER="unused",
               REMOTE_USER="unused", LOCAL_ROOT="/tmp", REMOTE_ROOT="/tmp", CG_USE_NTT_POLY="0")
    shell = f'source "{ROOT}/scripts/2pc_common.sh"\nrf_psi_expected_opa_points 4\ncommon_env'
    output = execute(["bash", "-c", shell], env=env).stdout
    assert output.startswith("9\n") and "CG_USE_NTT_POLY=0" in output and "CG_PUBLIC_SEED=" in output
    # Both log paths must occupy one CSV field despite the separating comma.
    csv_run = tmp / "csv-run"
    shell = f'''source "{ROOT}/scripts/2pc_common.sh"
OUT_DIR="{csv_run}"
timeout() {{ shift; "$@"; }}
ssh() {{ echo "mock role completed"; }}
init_run_dir csv_test
run_pair csv_test local_first true true
'''
    execute(["bash", "-c", shell], env=env)
    import csv
    with (csv_run / "runs.csv").open(newline="") as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == 1 and None not in rows[0]
    assert rows[0]["local_rc"] == rows[0]["remote_rc"] == "0"
    assert rows[0]["logs"].count(",") == 1
    # No watchdog descendants may retain the lock after child completion.
    lock = tmp / "lock"
    shell = f'''source "{ROOT}/scripts/run_common.sh"
exec {{fd}}>"{lock}"
flock -n "$fd"
true & child=$!
cg_wait_child "$child"
exec {{fd}}>&-
flock -n "{lock}" true
'''
    execute(["bash", "-c", shell], timeout=5)
    for path in list(ROOT.glob("*.py")) + list((ROOT / "tests").glob("*.py")):
        ast.parse(path.read_text(), filename=str(path))
    for path in list(ROOT.glob("*.sh")) + list((ROOT / "scripts").glob("*.sh")):
        execute(["bash", "-n", path])
    print("Python checker, generator, timing, environment, and syntax regressions passed", flush=True)

def free_ports(count):
    sockets = []
    try:
        for _ in range(count):
            fd = socket.socket()
            fd.bind(("127.0.0.1", 0))
            sockets.append(fd)
        return [str(fd.getsockname()[1]) for fd in sockets]
    finally:
        for fd in sockets: fd.close()

def protocol_checks(build, tmp):
    ports = free_ports(5)
    base = dict(os.environ, CG_VALIDATE_LANES="1",
                CG_BIND_ADDR="127.0.0.1", CG_RF_THREADS="2", CG_RF_LANES="2",
                CG_RF_CHUNK_SIZE="2", CG_IO_TIMEOUT_S="10", CG_CONNECT_RETRIES="100",
                CG_FIXED_Q="170141183460469232709364739622490341377",
                CG_Q_NBITS="128", CG_K="1", CG_PORT_REC=ports[0], CG_PORT_RFR=ports[1],
                CG_PORT_RFS=ports[2], CG_OPE_PORT_REC=ports[0], CG_PSI2_REVEAL_REC=ports[0],
                CG_PSI2_REVEAL_RFR=ports[1], CG_PSI2_REVEAL_RFS=ports[2])
    def group(name, commands, **settings):
        directory = tmp / name
        directory.mkdir()
        env = dict(base, CG_BENCH_IO_DIR=str(directory), CG_PROTOCOL_ID=name, **settings)
        processes, streams = [], []
        try:
            for i, command in enumerate(commands):
                stream = (directory / f"{i}.log").open("w")
                streams.append(stream)
                processes.append(subprocess.Popen([str(build / command[0]), *map(str, command[1:])],
                                                  env=env, stdout=stream, stderr=stream))
            deadline = time.monotonic() + 45
            while any(p.poll() is None for p in processes):
                assert time.monotonic() < deadline, f"{name}: timeout"
                if any(p.poll() not in (None, 0) for p in processes):
                    raise AssertionError(f"{name}: process failed")
                time.sleep(0.05)
            assert all(p.returncode == 0 for p in processes), name
        except Exception as exc:
            for stream in streams: stream.flush()
            details = "\n".join(path.read_text() for path in directory.glob("*.log"))
            raise AssertionError(f"{exc}\n{details}") from exc
        finally:
            for p in processes:
                if p.poll() is None: p.terminate()
            for p in processes:
                try: p.wait(timeout=3)
                except subprocess.TimeoutExpired: p.kill(); p.wait()
            for stream in streams: stream.close()
        print(f"Protocol passed: {name}", flush=True)
        return directory
    x, ab = tmp / "x", tmp / "ab"
    x.write_text("3\n4\n5\n"); ab.write_text("2 7\n9 1\n4 8\n")
    directory = group("direct-ole", [["cg_batch_ole_receiver", 3, ports[0], "--input-file", x],
                                       ["cg_batch_ole_sender", 3, "127.0.0.1", ports[0], "--input-file", ab]])
    execute([build / "cg_check_batch_ole", directory / "direct_ole_sender_inputs.txt", directory / "direct_ole_receiver_io.txt"])
    directory = group("rf-ole", [["cg_rf_batch_ole_receiver", 3, "--input-file", x],
                                  ["cg_rf_receiver_firewall_opa"], ["cg_rf_sender_firewall_opa"],
                                  ["cg_rf_batch_ole_sender", 3, "--input-file", ab]], CG_RF_EXPECTED_N="3")
    execute([build / "cg_check_batch_ole", directory / "rf_ole_sender_inputs.txt", directory / "rf_ole_receiver_io.txt"])
    directory = group("rf-ole-plain", [["cg_rf_batch_ole_receiver", 3, "--input-file", x],
                                         ["cg_rf_receiver_firewall_opa"], ["cg_rf_sender_firewall_opa"],
                                         ["cg_rf_batch_ole_sender", 3, "--input-file", ab]],
                      CG_RF_EXPECTED_N="3", CG_VALIDATE_LANES="0")
    execute([build / "cg_check_batch_ole", directory / "rf_ole_sender_inputs.txt", directory / "rf_ole_receiver_io.txt"])
    group("standalone-ole", [["cg_receiver", "127.0.0.1", ports[0], 3, 1],
                             ["cg_rrf", ports[0], "127.0.0.1", ports[1]],
                             ["cg_srf", ports[1], "127.0.0.1", ports[2]],
                             ["cg_sender", ports[2], 3, 1]])
    for rf in (False, True):
        commands = [["cg_rf_ole3_receiver" if rf else "cg_ole3_receiver", 3, ports[0]]]
        if rf: commands += [["cg_rf_receiver_firewall_ole3"], ["cg_rf_sender_firewall_ole3"]]
        commands += [["cg_rf_ole3_sender" if rf else "cg_ole3_sender", 3, "127.0.0.1", ports[2] if rf else ports[0]]]
        directory = group("rf-ole3" if rf else "direct-ole3", commands, CG_RF_EXPECTED_N="3")
        label = "rf_ole3" if rf else "ole3"
        checker = [build / "cg_check_batch_ole", directory / f"{label}_sender_inputs.txt",
                   directory / f"{label}_receiver_io.txt"]
        if rf:
            checker += [directory / "rf_ole3_sender_firewall_blinds.txt",
                        directory / "rf_ole3_receiver_firewall_blinds.txt"]
        execute(checker)
    polynomial, alpha = tmp / "poly", tmp / "alpha"
    polynomial.write_text("-1\n2\n3\n"); alpha.write_text("4\n")
    for rf in (False, True):
        prefix = "cg_rf_" if rf else "cg_"
        commands = [[prefix + "ope_receiver", 2, "--alpha-file", alpha]]
        if rf: commands += [["cg_rf_receiver_firewall_opa"], ["cg_rf_sender_firewall_opa"]]
        commands += [[prefix + "ope_sender", "127.0.0.1", 2, "--input-file", polynomial]]
        directory = group("rf-ope" if rf else "direct-ope", commands, CG_RF_EXPECTED_N="2")
        label = "rf" if rf else "direct"
        execute([build / "cg_check_ope", directory / f"{label}_ope_sender_coeffs.txt", directory / f"{label}_ope_receiver_output.txt"])
    receiver_poly = tmp / "receiver-poly"
    receiver_poly.write_text("-1\n1\n")
    for rf in (False, True):
        commands = [["cg_rf_opa_receiver" if rf else "cg_opa_receiver", 3, 1, "--input-file", receiver_poly]]
        if rf: commands += [["cg_rf_receiver_firewall_opa"], ["cg_rf_sender_firewall_opa"]]
        commands += [["cg_rf_opa_sender", 3, 1, "--input-file", polynomial]] if rf else [
            ["cg_opa_sender", "127.0.0.1", 3, 1, "--input-file", polynomial]]
        directory = group("rf-opa" if rf else "direct-opa", commands, CG_RF_EXPECTED_N="3")
        label = "rf" if rf else "direct"
        execute([build / "cg_check_opa", directory / f"{label}_opa_sender_coeffs.txt",
                 directory / f"{label}_opa_receiver_output.txt"])
    a, b, expected = tmp / "A", tmp / "B", tmp / "expected"
    a.write_text("0\n1\n3\n"); b.write_text("1\n2\n3\n"); expected.write_text("1\n3\n")
    for rf, ntt in [(False, "1"), (True, "1"), (True, "0")]:
        out = tmp / f"psi-{rf}-{ntt}.txt"
        commands = [["cg_rf_psi_receiver" if rf else "cg_psi_receiver", 3, "--input-file", b, "--output-file", out]]
        if rf:
            commands += [["cg_rf_receiver_firewall_opa"], ["cg_rf_sender_firewall_opa"], ["cg_rf_psi_sender", 3, "--input-file", a]]
        else:
            commands += [["cg_psi_sender", "127.0.0.1", 3, "--input-file", a]]
        group(f"psi-rf-{rf}-ntt-{ntt}", commands, CG_RF_EXPECTED_N="8" if ntt == "1" else "7", CG_USE_NTT_POLY=ntt)
        assert read_set(out) == {1, 3}
    out_a, out_b = tmp / "out-A", tmp / "out-B"
    group("direct-psi2-known-zero-limit", [["cg_psi2_receiver", "127.0.0.1", 3, "--input-file", b,
                            "--output-file", out_b, "--reveal-port", ports[4]],
                           ["cg_psi2_sender", "127.0.0.1", 3, "--input-file", a,
                            "--output-file", out_a, "--reveal-port", ports[4]]])
    assert read_set(out_b) == {1, 3}
    assert read_set(out_a) == {0, 1, 3}  # Same retained padding collision as RF PSI2.
    group("rf-reveal-known-zero-limit", [["cg_rf_psi2_reveal_receiver", "--input-file", expected],
                                        ["cg_rf_psi2_reveal_rrf"], ["cg_rf_psi2_reveal_srf"],
                                        ["cg_rf_psi2_reveal_sender", "--filter-file", a, "--output-file", out_a]],
          CG_PSI2_REVEAL_BOUND="3")
    assert read_set(out_a) == {0, 1, 3}  # Explicitly retained zero-padding limitation.
    print("Direct/RF zero padding retained; their documented limitation is explicit", flush=True)
    # Check launcher count propagation and generated input bounds.
    for name, script, arguments in [
        ("local-rf-ole", "run_rf_cg_batch_ole_local.sh", [3]),
        ("local-rf-ope", "run_rf_cg_ope_local.sh", [2]),
        ("local-rf-psi", "run_rf_cg_psi_local.sh", ["--files", a, b, expected]),
        ("local-direct-psi", "run_cg_psi_local.sh", [5, 5, 1, 42]),
    ]:
        directory = tmp / name
        env = dict(base, CG_BUILD_DIR=str(build), CG_RUN_LOG_DIR=str(directory),
                   CG_BENCH_IO_DIR=str(directory / "io"), CG_PSI_INPUT_DIR=str(directory / "inputs"),
                   CG_RUN_LOCK=str(tmp / "run.lock"), CG_PSI_PORT_REC=ports[0], CG_RF_EXPECTED_N="999")
        execute(["bash", ROOT / script, *arguments], env=env, timeout=45)
        print(f"Launcher passed: {name}", flush=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--cpp-bin", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="cg-regression-") as directory:
        tmp = Path(directory)
        if not args.cpp_bin and args.build_dir and (args.build_dir / "cg_regression").exists():
            args.cpp_bin = args.build_dir / "cg_regression"
        exe = args.cpp_bin.resolve() if args.cpp_bin else tmp / "regression"
        if not args.cpp_bin:
            execute(["g++", "-std=c++17", "-O0", "-fopenmp", "-pthread", "-I" + str(ROOT),
                     "-I" + str(ROOT / "third_party"), "-I" + str(ROOT / "third_party/cg_ahe"),
                     ROOT / "tests/regression.cpp", "-o", exe, "-lgmpxx", "-lgmp", "-lcrypto"], timeout=90)
        execute([exe, tmp], timeout=20)
        for test in ("tcp", "count", "wrong-domain"):
            execute([exe, "transport", test], timeout=20)
        print("C++ and TCP transport regressions passed", flush=True)
        python_checks(tmp)
        if args.build_dir: protocol_checks(args.build_dir.resolve(), tmp)
    print("All regression checks passed", flush=True)

if __name__ == "__main__":
    main()
