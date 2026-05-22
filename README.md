# Class-Group Reverse-Firewall OLE / OPA / PSI

This folder is a standalone C++17 demo/benchmark package for reverse-firewalled
protocols over Class-Group additively homomorphic encryption:

- RF-OLE: receiver input `x`, sender inputs `a,b`, receiver obtains `a*x+b`.
- RF-OPA: oblivious polynomial addition built from batched RF-OLE.
- RF-PSI: private set intersection built from RF-OPA.

The CG-AHE wrapper and BICYCL headers used by the demos are vendored in
`third_party/cg_ahe`, so this directory can be uploaded as its own repository.

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
scripts. They default to receiver side `party_b_user@PARTY_B_IP`, sender side
`party_a_user@PARTY_A_IP`, remote build directory `build-2pc`, and local checker
directory `build-portable`.

```bash
bash run_2pc_direct_ole.sh 1000
bash run_2pc_rf_ole.sh 1000
bash run_2pc_direct_ope.sh 1000
bash run_2pc_rf_ope.sh 1000
bash run_2pc_direct_opa.sh 1000
bash run_2pc_rf_opa.sh 1000
bash run_2pc_direct_psi.sh 1000
bash run_2pc_rf_psi.sh 1000
bash run_2pc_rf_psi2.sh 1000
```

Common overrides:

```bash
LOCAL_IP=PARTY_B_IP REMOTE_IP=PARTY_A_IP CG_RF_LANES=8 bash run_2pc_rf_psi.sh 1000
```

See `RUN_2PC_FROM_10_5_31_190.md` for the full sync/build/run workflow.

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

## Polynomial Layer

OPA batch-evaluates `p_A`, `r_A`, and `p_B` with a product-tree multipoint
evaluator over the CG-AHE plaintext modulus `q`. PSI set-polynomial construction
also uses the same balanced product tree for `prod(X - item)`. The modulus is
still the random CG plaintext prime, so this is not an NTT-specific modulus
change.

## Main Files

- `cg_receiver.cpp`, `cg_rrf.cpp`, `cg_srf.cpp`, `cg_sender.cpp`: active
  four-process batched RF-OLE.
- `cg_rf_ole_batch.hpp`: shared batched RF-OLE endpoint helper for OPA/PSI.
- `cg_rf_opa.hpp`: RF-OPA reduction to batched RF-OLE.
- `cg_rf_psi_sender.cpp`, `cg_rf_psi_receiver.cpp`: RF-PSI reduction to RF-OPA.
- `generate_sets.py`, `check_correctness.py`: 128-bit plaintext PSI test-data
  generation and external receiver-output checking.
- `cg_bench.cpp`: standalone no-socket benchmark/simulation.
- `OPERATION_COMMENTS.md`: role-by-role protocol notes.

## Artifact Layout

Active source, scripts, and documentation live at the repository root.
Generated build/log/input/benchmark outputs are ignored by git. Older full
simulation OLE files that are not part of the active artifact build are parked
under `archive_unused/fs_ole/`.

## Notes

This is research/demo code. It favors protocol clarity and local benchmarking
over production hardening. The localhost demo scripts use fixed ports
`9001`, `9002`, and `9003`; make sure no old demo process is still running if a
script reports a bind/connect failure.
