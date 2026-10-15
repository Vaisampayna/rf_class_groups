# Running Local Tests

Run these commands from the `rf_class_groups` directory after building:

```bash
sudo apt-get install -y build-essential cmake libgmp-dev libssl-dev libntl-dev
cmake -S . -B build
cmake --build build -j2
export CG_BUILD_DIR="$PWD/build"
export CG_RF_THREADS=2
```

## Batched RF-OLE

```bash
CG_RF_LANES=4 bash run_rf_cg_batch_ole_local.sh 1000
```

The script starts the receiver, receiver firewall, sender firewall, and sender
on localhost, then writes logs under `logs/`.

## RF-OPA

```bash
CG_RF_LANES=4 bash run_rf_cg_opa_local.sh 2 "1 2 1 0 0" "-1 0 1"
```

Arguments are `d`, sender polynomial `p_A` of degree at most `2d`, and receiver
polynomial `p_B` of degree at most `d`, with coefficients written in
constant-to-highest-degree order. The sender samples the mask polynomial `r_A`
internally.

## RF-PSI

Generated 128-bit plaintext set inputs:

```bash
CG_RF_LANES=4 bash run_rf_cg_psi_local.sh 100 100 20 42
```

Existing set files:

```bash
CG_RF_LANES=4 bash run_rf_cg_psi_local.sh --files set_A.txt set_B.txt true_intersection.txt
```

The script writes `logs/run_<session>/intersection_out.txt` and runs the external correctness
checker against `true_intersection.txt`.

## Two-Machine Scripts

For the reported two-machine experiments, use the `run_2pc_*.sh` entrypoints
and sweep scripts described in `RUN_2PC_SETUP.md`. The full paper-table driver
is `benchmark_2pc_paper_all.sh`.

## Logs

List the generated process logs with:

```bash
find logs -name '*.log' -type f -print
```

## Automated regression suite

After building with `BUILD_TESTING=ON` (the default):

```bash
ctest --test-dir build --output-on-failure
```

Local launchers isolate logs under `logs/run_<session>/`. Protocol sockets use
TCP. Tests cover TCP serialization,
optional parameter checks, input validation, timing completeness, and small
protocol runs using temporary files and local sockets. The two-way PSI tests
also verify the documented zero-padding collision. No SSH is required.
