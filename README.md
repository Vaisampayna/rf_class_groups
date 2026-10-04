# Class-Group Reverse-Firewall OLE / OPE / OPA / PSI

This folder is a standalone C++17 demo/benchmark package for reverse-firewalled
protocols over Class-Group additively homomorphic encryption:

- RF-OLE: receiver input `x`, sender inputs `a,b`, receiver obtains `a*x+b`.
- RF-OPA: oblivious polynomial addition built from batched RF-OLE.
- RF-PSI: private set intersection built from RF-OPA.

The reported experiments cover exactly these rows: batched OLE, batched RF-OLE,
3-round OLE, 3-round RF-OLE, OPE, RF-OPE, OPA, RF-OPA, one-way PSI, one-way
RF-PSI, direct two-way PSI, and RF two-way PSI. The repository contains the
implementation and scripts for these rows.

The CG-AHE wrapper and BICYCL headers used by the demos are included under
`third_party/cg_ahe`, making the artifact self-contained apart from system
libraries.

## Dependencies

Ubuntu/Debian packages:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake libgmp-dev libssl-dev libntl-dev
```

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

The CMake build creates the reported OLE/OPE/OPA/PSI binaries and the local
correctness helpers used by the scripts.

For paper-style timing runs on each protocol host, use a release/native build:

```bash
cmake -S . -B build-2pc \
  -DCMAKE_BUILD_TYPE=Release \
  -DCG_ENABLE_NATIVE_ARCH=ON \
  -DCG_ENABLE_LTO=ON \
  -DCG_USE_NTL_POLY=ON
cmake --build build-2pc -j
```

The configure output should say that NTL was found. Without NTL the code still
builds, but PSI timings will not match the paper configuration.

## Quick Runs

Batched RF-OLE:

```bash
bash run_rf_cg_batch_ole_local.sh 100
```

OPA:

```bash
bash run_rf_cg_opa_local.sh 1 "1 2 3" "-1 1"
```

The RF-OPA local command has the form:

```bash
bash run_rf_cg_opa_local.sh <d> "<a coefficients>" "<b coefficients>"
```

Coefficients are written in constant-term-first order. The sender's masking
polynomial `r` is sampled freshly inside the sender binary; it is not a command
line input. In the example above:

```text
d = 1
a(X) = 1 + 2X + 3X^2      degree <= 2d
b(X) = -1 + X             degree <= d
r(X) is sampled internally degree <= d
```

The receiver obtains evaluations of:

```text
a(X) + r(X)b(X)
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
export CHECKER_DIR="$PWD/build"
```

```bash
bash run_2pc_direct_ole.sh 1000
bash run_2pc_rf_ole.sh 1000
bash run_2pc_direct_ole3.sh 1000
bash run_2pc_rf_ole3.sh 1000
bash run_2pc_direct_ope.sh 1000
bash run_2pc_rf_ope.sh 1000
bash run_2pc_direct_opa.sh 1000
bash run_2pc_rf_opa.sh 1000
bash run_2pc_direct_psi.sh 1000
bash run_2pc_direct_psi2.sh 1000
bash run_2pc_rf_psi.sh 1000
bash run_2pc_rf_psi2.sh 1000
```

The argument to these scripts is the paper-table benchmark size `N`. For OLE,
OPE, and PSI this is direct: it is the number of OLE slots, polynomial degree
scale, or set size. For OPA, `N` is the number of public OPA evaluation/OLE
slots. The scripts set `d=floor((N-2)/2)`, choose `deg(a)<=2d`,
`deg(r)<=d`, and `deg(b)<=d`, so `2d+1 <= N`. For example,
`bash run_2pc_rf_opa.sh 8192` uses `d=4095`, `deg(a)<=8190`,
`deg(r),deg(b)<=4095`, and exactly `8192` RF-OLE slots.

For two-way PSI (`run_2pc_direct_psi2.sh` and `run_2pc_rf_psi2.sh`), the first
phase is the corresponding one-way PSI protocol, where Party B learns the
intersection. The second phase reveals the same intersection back to Party A.
The reveal-back phase does not send `intersection.size()` as a separate
protocol message. Instead, it sends a fixed number of reveal slots, and Party A
keeps only revealed values that occur in its own input set.

For RF two-way PSI, this fixed reveal count is controlled by
`CG_PSI2_REVEAL_BOUND`. If unset, the wrapper uses the conservative value
`N`, so the return path sends `N` encrypted reveal slots:

```bash
CG_PSI2_REVEAL_BOUND=32768 bash run_2pc_rf_psi2.sh 32768
```

This is the cleanest setting when no public upper bound on the intersection
size is assumed, but it is slower than the original variable-size reveal-back.
For the paper benchmark generator, the overlap is public and defaults to
`N/5`; in that case a paper-like run can set the public reveal bound to the
known overlap:

```bash
OVERLAP=$((32768 / 5))
CG_PSI2_REVEAL_BOUND=$OVERLAP bash run_2pc_rf_psi2.sh 32768
```

Use `CG_PSI2_REVEAL_BOUND=N` to audit the full-padding overhead, and use the
public-overlap value to reproduce timings closer to the reported table. The
paper sweep wrappers, `benchmark_2pc_paper_all.sh` and
`run_2pc_rf_psi2_sweep.sh`, use this public-overlap reveal bound by default
unless `CG_PSI2_REVEAL_BOUND` is already set by the caller.

Common overrides:

```bash
LOCAL_IP=<party-b-ip> REMOTE_IP=<party-a-ip> CG_RF_LANES=8 bash run_2pc_rf_psi.sh 1000
```

Sweep scripts are provided for the table-style experiments, including
`benchmark_2pc_paper_all.sh`, `run_2pc_direct_psi2_sweep.sh`, and
`run_2pc_rf_psi2_sweep.sh`. The recommended paper reproduction entrypoint is:

```bash
bash benchmark_2pc_paper_all.sh
python3 make_reported_total_table.py benchmark_2pc_paper_all_...
```

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
CG_RF_LANES=4 bash run_rf_cg_opa_local.sh 1 "1 2 3" "-1 1"
CG_RF_LANES=4 bash run_rf_cg_psi_local.sh 1000 1000 200 42
```

The default is one lane, which preserves the original single-connection
behavior. The same batched RF-OLE helper is used by RF-OPA, and RF-PSI builds on
that RF-OPA layer.

For non-paper "fast run" experiments, use the fast preset helper. It keeps the
same protocol code path but uses a larger RF chunk size and disables detailed
firewall operation profiling:

```bash
bash run_2pc_fast_preset.sh ./run_2pc_rf_opa.sh 8192
```

The paper reproduction driver does not use this helper by default, so published
configuration and additional optimized experiments remain separate.

## Polynomial Layer

OPA batch-evaluates the sender and receiver polynomials over the CG-AHE
plaintext field `Z_q`. For reproducible paper benchmarks, the scripts set
`CG_FIXED_Q=170141183460469232709364739622490341377`, an NTT-friendly 128-bit
prime, and `CG_USE_NTT_POLY=1`. PSI/PSI2 then pads the OPA evaluation domain to
the next power of two with `psi_opa_point_count(...)`, so the NTT path is used
for the large polynomial interpolation/evaluation layer. NTL is also used for
large finite-field polynomial operations when `libntl` is available.

## Benchmark Size Convention

The paper table columns are labeled by `N = 2^10, ..., 2^15`. The scripts use
that same convention:

- Batched OLE / RF-OLE: `N` OLE instances.
- 3-round OLE / 3-round RF-OLE: `N` OLE instances.
- OPE / RF-OPE: degree-scale `N`, implemented with about `N` OLE slots.
- OPA / RF-OPA: `N` public evaluation/OLE slots. The scripts choose
  `d=floor((N-2)/2)`, so Party A's additive polynomial has degree at most
  `2d`, Party A's random mask polynomial has degree at most `d`, and Party B's
  polynomial has degree at most `d`. Since `2d+1 <= N`, the outputs determine
  the masked degree-`2d` polynomial; for power-of-two `N`, the public OPA
  evaluation domain is NTT-friendly.
- One-way PSI / RF-PSI: both sets have size `N`.
- Two-way PSI / RF-PSI: both sets have size `N`; the two-way variant is
  one-way PSI plus a reveal-back phase. Direct two-way PSI sends fixed
  plaintext reveal slots from Party B to Party A. RF two-way PSI sends fixed
  encrypted reveal slots through the two reverse firewalls; the firewalls
  inverse-maul and rerandomize each ciphertext on the return path. The fixed
  reveal slot count is `CG_PSI2_REVEAL_BOUND`, defaulting to `N` in
  `run_2pc_rf_psi2.sh`.


## Main Files

- `cg_batch_ole_receiver.cpp`, `cg_batch_ole_sender.cpp`,
  `cg_rf_batch_ole_receiver.cpp`, `cg_rf_batch_ole_sender.cpp`: batched OLE and
  batched RF-OLE.
- `cg_ole3_receiver.cpp`, `cg_ole3_sender.cpp`, `cg_rf_ole3_receiver.cpp`,
  `cg_rf_ole3_sender.cpp`: 3-round OLE and 3-round RF-OLE.
- `cg_ope_receiver.cpp`, `cg_ope_sender.cpp`, `cg_rf_ope_receiver.cpp`,
  `cg_rf_ope_sender.cpp`: OPE and RF-OPE.
- `cg_opa_receiver.cpp`, `cg_opa_sender.cpp`, `cg_rf_opa_receiver.cpp`,
  `cg_rf_opa_sender.cpp`: OPA and RF-OPA.
- `cg_rf_ole_batch.hpp`: shared batched RF-OLE endpoint helper for OPA/PSI.
- `cg_rf_opa.hpp`: RF-OPA reduction to batched RF-OLE.
- `cg_rf_psi_sender.cpp`, `cg_rf_psi_receiver.cpp`: RF-PSI reduction to RF-OPA.
- `cg_psi_sender.cpp`, `cg_psi_receiver.cpp`: direct one-way PSI.
- `cg_psi2_sender.cpp`, `cg_psi2_receiver.cpp`: direct two-way PSI, implemented
  as direct one-way PSI followed by a fixed-slot clear reveal-back. The sender
  filters received values against its own input set.
- `cg_rf_psi2_reveal_sender.cpp`, `cg_rf_psi2_reveal_receiver.cpp`,
  `cg_rf_psi2_reveal_srf.cpp`, `cg_rf_psi2_reveal_rrf.cpp`: RF two-way PSI
  fixed-slot encrypted reveal-back phase.
- `generate_sets.py`, `check_correctness.py`: 128-bit plaintext PSI test-data
  generation and external receiver-output checking.
- `OPERATION_COMMENTS.md`: role-by-role protocol notes.

## Notes

This is research/demo code. It favors protocol clarity and local benchmarking
over production hardening. The localhost demo scripts use fixed ports
`9001`, `9002`, and `9003`; make sure no demo process is still running if a
script reports a bind/connect failure.
