# Class-Group Reverse-Firewall OLE / OPA / PSI

This folder is a standalone C++17 demo/benchmark package for reverse-firewalled
protocols over Class-Group additively homomorphic encryption:

- RF-OLE: receiver input `x`, sender inputs `a,b`, receiver obtains `a*x+b`.
- RF-OPA: oblivious polynomial addition built from batched RF-OLE.
- RF-PSI: private set intersection built from RF-OPA.

The CG-AHE wrapper and BICYCL headers used by the demos are included under
`third_party/cg_ahe`, making the artifact self-contained for building and
reproducing the experiments.

## Dependencies

Ubuntu/Debian packages:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libgmp-dev libssl-dev
```

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

The CMake build creates both the active batched RF-OLE binaries and the local
OLE/OPA/PSI demo binaries used by the scripts.

## Quick Runs

Single OLE:

```bash
bash run_rf_cg_ole_local.sh 5 3 7
```

OPA:

```bash
bash run_rf_cg_opa_local.sh 2 "1 2 1" "1 1 1" "-1 0 1"
```

PSI, with 128-bit plaintext file inputs and an external correctness check:

```bash
bash run_rf_cg_psi_local.sh 100 100 20 42
```

The PSI script generates `inputs/psi/set_A.txt`, `inputs/psi/set_B.txt`, and
`inputs/psi/true_intersection.txt`, runs the protocol with sender and receiver
reading their private inputs from those files, writes the receiver output to
`logs/intersection_out.txt`, then checks it with `check_correctness.py`.

To use existing files:

```bash
bash run_rf_cg_psi_local.sh --files set_A.txt set_B.txt true_intersection.txt
```

The scripts create `build/` if needed and write process logs to `logs/`.

## Two-Machine Protocol Entrypoints

From the controller machine, run one protocol at a time with the `run_2pc_*.sh`
scripts. Set the two party addresses, users, repository paths, build directory,
and checker directory through environment variables:

```bash
export LOCAL_IP=<party-b-ip>
export REMOTE_IP=<party-a-ip>
export LOCAL_USER=<party-b-user>
export REMOTE_USER=<party-a-user>
export LOCAL_ROOT=<party-b-repo-path>
export REMOTE_ROOT=<party-a-repo-path>
export BUILD_DIR=build-2pc
export CHECKER_DIR="$PWD/build-portable"
```

```bash
bash run_2pc_direct_ole.sh 1000
bash run_2pc_rf_ole.sh 1000
bash run_2pc_direct_ope.sh 1000
bash run_2pc_rf_ope.sh 1000
bash run_2pc_direct_opa.sh 1000
bash run_2pc_rf_opa.sh 1000
bash run_2pc_direct_psi.sh 1000
bash run_2pc_direct_psi2.sh 1000
bash run_2pc_rf_psi.sh 1000
bash run_2pc_rf_psi2.sh 1000
```

Common overrides:

```bash
LOCAL_IP=<party-b-ip> REMOTE_IP=<party-a-ip> CG_RF_LANES=8 bash run_2pc_rf_psi.sh 1000
```

Sweep scripts are provided for the table-style experiments, including
`benchmark_2pc_cg_sweep.sh`, `benchmark_2pc_ole3_sweep.sh`,
`benchmark_2pc_psi_fast_sweep.sh`, `run_2pc_direct_psi2_sweep.sh`, and
`run_2pc_rf_psi2_sweep.sh`.

All `run_2pc_*.sh` entrypoints use file-backed benchmark inputs. OLE, OPE,
OPA, one-way PSI, and two-way PSI also run external correctness checks after the
protocol process exits. Checker outputs are written to each run directory's
`checks.csv` and `io/<run>/check.log`.

## Parallel Transport

The batched RF-OLE path, RF-OPA, and RF-PSI can use multiple parallel TCP
connections between each adjacent pair of parties. Set `CG_RF_LANES` before
running the local scripts:

```bash
CG_RF_LANES=4 bash run_rf_cg_batch_ole_local.sh 1000
CG_RF_LANES=4 bash run_rf_cg_opa_local.sh 2 2 "1 2 1" "1 1 1" "-1 0 1"
CG_RF_LANES=4 bash run_rf_cg_psi_local.sh 1000 1000 200 42
```

The default is one lane, which preserves the original single-connection
behavior. The same batched RF-OLE helper is used by RF-OPA, and RF-PSI builds on
that RF-OPA layer.



## Main Files

- `cg_receiver.cpp`, `cg_rrf.cpp`, `cg_srf.cpp`, `cg_sender.cpp`: active
  four-process batched RF-OLE.
- `cg_rf_ole_batch.hpp`: shared batched RF-OLE endpoint helper for OPA/PSI.
- `cg_rf_opa.hpp`: RF-OPA reduction to batched RF-OLE.
- `cg_rf_psi_sender.cpp`, `cg_rf_psi_receiver.cpp`: RF-PSI reduction to RF-OPA.
- `cg_psi2_sender.cpp`, `cg_psi2_receiver.cpp`: direct two-way PSI, implemented
  as direct one-way PSI followed by a clear reveal-back of the intersection.
- `cg_rf_psi2_reveal_sender.cpp`, `cg_rf_psi2_reveal_receiver.cpp`,
  `cg_rf_psi2_reveal_srf.cpp`, `cg_rf_psi2_reveal_rrf.cpp`: RF two-way PSI
  reveal-back phase.
- `generate_sets.py`, `check_correctness.py`: 128-bit plaintext PSI test-data
  generation and external receiver-output checking.
- `cg_bench.cpp`: standalone no-socket benchmark/simulation.
- `OPERATION_COMMENTS.md`: role-by-role protocol notes.



## Notes

 The localhost demo scripts use fixed ports
`9001`, `9002`, and `9003`; make sure no old demo process is still running if a
script reports a bind/connect failure.
