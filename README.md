# Class-Group OLE, OPE, OPA, and PSI

Research proof-of-concept implementations of batched OLE, 3-round OLE, OPE,
OPA, and one-way and two-way PSI, together with their reverse-firewall (RF)
variants. This guide covers installation, local tests, two-machine setup,
benchmark commands, and result checking.

## 1. Get the source and install dependencies

Use Ubuntu/Debian with Bash, a C++17 compiler, CMake, Python 3, GMP, OpenSSL's
Crypto library, and NTL. For two-machine runs, the controller also needs SSH
and SCP, and each protocol host needs an SSH server.

On the controller:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake python3 git openssl \
  libgmp-dev libssl-dev libntl-dev openssh-client

git clone https://github.com/Vaisampayna/rf_class_groups.git
cd rf_class_groups
```

If you already have the source, change into that directory instead of cloning
again. For artifact evaluation, check out the commit or tag given in the
submission before building or copying the source:

```bash
git checkout 'SUBMITTED_COMMIT_OR_TAG'
```

Replace `SUBMITTED_COMMIT_OR_TAG` with the actual submitted revision.
All subsequent controller commands assume the repository is the current directory.

## 2. Build and check the installation

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCG_USE_NTL_POLY=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

`-j2` uses two parallel build jobs. Increase it if the machine has enough memory.
CMake should report `Using NTL for large PSI polynomial operations:` followed
by the library path. If NTL is missing, CMake falls back to built-in polynomial
code; install `libntl-dev` and reconfigure before reproducing benchmark timings.

A successful test run ends with `100% tests passed`. The suite checks parsers,
protocol outputs, launchers, timing completeness, and the known padding case
in Section 9. The local suite previously took about 2–3 minutes on the
validation machine; runtime depends on the host.

## 3. Run local examples

These examples start all required protocol roles on one machine. Run them one
at a time, from the repository directory:

```bash
export CG_BUILD_DIR="$PWD/build"
export CG_RF_THREADS=2
```

`CG_BUILD_DIR` selects the compiled binaries. `CG_RF_THREADS` selects the worker
threads per protocol process; `2` is suitable for a small functional test.

### RF-PSI with generated sets

```bash
CG_RF_LANES=4 bash run_rf_cg_psi_local.sh 100 100 20 42
```

| Part of the command | Meaning |
| --- | --- |
| `CG_RF_LANES=4` | Use four parallel TCP connections between adjacent RF protocol roles. |
| `bash run_rf_cg_psi_local.sh` | Start the receiver, receiver RF, sender RF, and sender on localhost. |
| First `100` | Number of elements in Party A's sender set. |
| Second `100` | Number of elements in Party B's receiver set. |
| `20` | Number of common elements generated in the two sets. |
| `42` | Seed for generating the test sets. |

The overlap must be between zero and the smaller set size. The seed controls
test inputs; it does not fix protocol encryption randomness. The inline
`CG_RF_LANES=4` setting applies only to this command. To reuse it for subsequent
commands, run `export CG_RF_LANES=4` instead. Lanes are network connections,
not worker threads.

The receiver learns the intersection. The script writes the generated inputs,
intersection, and role logs under `logs/run_<session>/` and runs the checker.
For this example, successful output includes:

```text
SUCCESS: Receiver exactly output the correct intersection of 20 elements!
```

The direct PSI equivalent uses the same four positional arguments:

```bash
bash run_cg_psi_local.sh 100 100 20 42
```

### PSI with existing files

Create a small example from the repository directory:

```bash
mkdir -p example_inputs
printf '10\n20\n30\n' > example_inputs/set_A.txt
printf '20\n30\n40\n' > example_inputs/set_B.txt
printf '20\n30\n' > example_inputs/expected.txt

CG_RF_LANES=4 bash run_rf_cg_psi_local.sh --files \
  example_inputs/set_A.txt example_inputs/set_B.txt example_inputs/expected.txt
```

`--files` takes Party A's set, Party B's set, and the expected intersection,
in that order. The third file is used only for the offline correctness check.
Each file contains one unique decimal integer per line. Set elements must be
in `[0,q-1]`. The example should report a correct intersection of two elements.
To run direct PSI, replace the script name with `run_cg_psi_local.sh`.

### OLE, OPE, and OPA examples

```bash
CG_RF_LANES=4 bash run_rf_cg_batch_ole_local.sh 100
bash run_cg_ole3_local.sh 100
CG_RF_LANES=4 bash run_rf_cg_ope_local.sh 10
bash run_cg_ope_local.sh 10
CG_RF_LANES=4 bash run_rf_cg_opa_local.sh 1 "1 2 3" "-1 1"
bash run_cg_opa_local.sh 1 "1 2 3" "-1 1"
```

The OLE argument is the number of OLE instances. The OPE argument is the
sender polynomial degree. OPA takes `d`, the coefficients of `a(X)`, and the
coefficients of `b(X)`. Coefficients are constant-term first, so the OPA example
uses `d=1`, `a(X)=1+2X+3X^2`, and `b(X)=-1+X`. The sender samples a mask
`r(X)` internally; the receiver obtains evaluations of `a(X)+r(X)b(X)`.
The bounds are `deg(a)<=2d`, `deg(b)<=d`, and `deg(r)<=d`.

## 4. Configure the two-machine setup

There are two protocol hosts and a controller. The controller can also be one
of the protocol hosts. It launches both sides over SSH and collects results;
you do not need to start the roles manually in separate terminals.

Use reachable LAN addresses. Replace all six example values below with your
own accounts and absolute paths. Run this block on the controller and keep
using the same shell:

```bash
export LOCAL_IP="192.168.1.10"
export REMOTE_IP="192.168.1.11"
export LOCAL_USER="receiver"
export REMOTE_USER="sender"
export LOCAL_ROOT="/home/receiver/rf_class_groups"
export REMOTE_ROOT="/home/sender/rf_class_groups"
export BUILD_DIR="build-2pc"
export CHECKER_DIR="$PWD/build-portable"
```

| Variable | Meaning |
| --- | --- |
| `LOCAL_IP`, `LOCAL_USER` | Address and login account for Party B, the receiver host. |
| `REMOTE_IP`, `REMOTE_USER` | Address and login account for Party A, the sender host. |
| `LOCAL_ROOT` | Repository directory on the receiver host. |
| `REMOTE_ROOT` | Repository directory on the sender host. |
| `BUILD_DIR` | Build-directory name inside each host's repository. |
| `CHECKER_DIR` | Absolute path to the controller's compiled offline checkers. |

`LOCAL` refers to the receiver role, even when that host is a separate machine
from the controller. Use a fresh source directory on each host for the initial
setup. Both protocol hosts must be able to reach each other's protocol ports.

### Set up passwordless SSH from the controller

First ensure an SSH server is running on each protocol host. Run these commands
on each host using its terminal:

```bash
sudo apt-get install -y openssh-server
sudo systemctl enable --now ssh
```

On the controller, create a default SSH key only if one does not already exist:

```bash
if [ ! -f "$HOME/.ssh/id_ed25519" ]; then
  ssh-keygen -t ed25519 -f "$HOME/.ssh/id_ed25519" -N ""
fi

ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$LOCAL_USER@$LOCAL_IP"
ssh-copy-id -i "$HOME/.ssh/id_ed25519.pub" "$REMOTE_USER@$REMOTE_IP"
```

`ssh-copy-id` may request each account's password during this initial setup.
If an existing key has a passphrase, load it into an SSH agent before testing:

```bash
eval "$(ssh-agent -s)"
ssh-add "$HOME/.ssh/id_ed25519"
```

Verify that both controller connections work without an interactive prompt:

```bash
ssh -o BatchMode=yes "$LOCAL_USER@$LOCAL_IP" 'hostname'
ssh -o BatchMode=yes "$REMOTE_USER@$REMOTE_IP" 'hostname'
```

Both commands must print the remote hostname and exit successfully. The
benchmark scripts use `BatchMode=yes`; they cannot answer password prompts.
SSH access is needed from the controller to both hosts; the protocol hosts do
not need to SSH into each other.

### Install host dependencies and copy the source

From the controller:

```bash
ssh -t "$LOCAL_USER@$LOCAL_IP" \
  'sudo apt-get update && sudo apt-get install -y build-essential cmake python3 openssl libgmp-dev libssl-dev libntl-dev'
ssh -t "$REMOTE_USER@$REMOTE_IP" \
  'sudo apt-get update && sudo apt-get install -y build-essential cmake python3 openssl libgmp-dev libssl-dev libntl-dev'
```

These installation commands may ask for a `sudo` password. Benchmark runs do
not require `sudo`.

Create a source package on the controller and copy the same package to both
hosts. This excludes local build directories and old experiment results:

```bash
python3 scripts/package_artifact.py
scp dist/rf_class_groups_artifact.tar.gz "$LOCAL_USER@$LOCAL_IP:rf_class_groups_artifact.tar.gz"
scp dist/rf_class_groups_artifact.tar.gz "$REMOTE_USER@$REMOTE_IP:rf_class_groups_artifact.tar.gz"

ssh "$LOCAL_USER@$LOCAL_IP" \
  "mkdir -p '$LOCAL_ROOT' && tar -xzf ~/rf_class_groups_artifact.tar.gz -C '$LOCAL_ROOT' --strip-components=1"
ssh "$REMOTE_USER@$REMOTE_IP" \
  "mkdir -p '$REMOTE_ROOT' && tar -xzf ~/rf_class_groups_artifact.tar.gz -C '$REMOTE_ROOT' --strip-components=1"
```

If the controller is a protocol host, choose a separate destination directory
for this extracted copy so that it does not overwrite the controller checkout.

## 5. Build on both hosts and on the controller

Build directly on each protocol host, since native CPU tuning can differ
between machines:

```bash
ssh "$LOCAL_USER@$LOCAL_IP" \
  "cd '$LOCAL_ROOT' && cmake -S . -B '$BUILD_DIR' -DCMAKE_BUILD_TYPE=Release -DCG_ENABLE_NATIVE_ARCH=ON -DCG_ENABLE_LTO=ON -DCG_USE_NTL_POLY=ON && cmake --build '$BUILD_DIR' -j2"

ssh "$REMOTE_USER@$REMOTE_IP" \
  "cd '$REMOTE_ROOT' && cmake -S . -B '$BUILD_DIR' -DCMAKE_BUILD_TYPE=Release -DCG_ENABLE_NATIVE_ARCH=ON -DCG_ENABLE_LTO=ON -DCG_USE_NTL_POLY=ON && cmake --build '$BUILD_DIR' -j2"

cmake -S . -B "$CHECKER_DIR" -DCMAKE_BUILD_TYPE=Release \
  -DCG_ENABLE_NATIVE_ARCH=OFF -DCG_ENABLE_LTO=ON -DCG_USE_NTL_POLY=ON
cmake --build "$CHECKER_DIR" -j2
```

Check the configure output on both protocol hosts for the NTL library message
shown in Section 2. `CG_ENABLE_NATIVE_ARCH=ON` enables CPU-specific instructions;
build separately on each machine rather than copying compiled binaries.
`CG_ENABLE_LTO=ON` enables link-time optimization when supported.

## 6. Select benchmark settings and run a smoke test

On the controller, in the same shell as the machine settings:

```bash
export CG_Q_NBITS=128 CG_K=1
export CG_FIXED_Q=170141183460469232709364739622490341377
export CG_USE_NTT_POLY=1
export CG_BENCH_INPUT_BITS=128 CG_PSI_INPUT_BITS=128 CG_INPUT_FILE_BITS=128
export CG_RF_LANES=8 CG_RF_CHUNK_SIZE=128
export CG_RF_THREADS_LOCAL=28 CG_RF_THREADS_REMOTE=32
export CG_RF_THREADS_PSI2_LOCAL=28 CG_RF_THREADS_PSI2_REMOTE=32
export CG_VALIDATE_LANES=0 CG_SKIP_CHECKS=0
export CG_IO_TIMEOUT_S=180 CG_CONNECT_RETRIES=7200 TIMEOUT_S=7200
unset OUT_DIR OVERLAP CG_PSI2_REVEAL_BOUND
```

| Setting | Meaning |
| --- | --- |
| `CG_Q_NBITS`, `CG_K`, `CG_FIXED_Q` | Plaintext and class-group scheme parameters. |
| `CG_USE_NTT_POLY=1` | Enable the NTT polynomial path for supported parameters. |
| `CG_BENCH_INPUT_BITS`, `CG_PSI_INPUT_BITS`, `CG_INPUT_FILE_BITS` | Bit bounds for generated benchmark inputs; PSI elements are also bounded by `q`. |
| `CG_RF_LANES=8` | Parallel TCP lanes per adjacent connection in the RF pipeline. |
| `CG_RF_CHUNK_SIZE=128` | Ciphertexts processed per transport chunk. |
| `CG_RF_THREADS_LOCAL`, `CG_RF_THREADS_REMOTE` | Worker threads per protocol process on the receiver and sender hosts. |
| `CG_RF_THREADS_PSI2_LOCAL`, `CG_RF_THREADS_PSI2_REMOTE` | Separate per-process thread counts for RF two-way PSI. |
| `CG_VALIDATE_LANES=0` | Use the default lane framing for timing reproduction. |
| `CG_SKIP_CHECKS=0` | Run the offline correctness checkers. |
| `CG_IO_TIMEOUT_S` | Socket I/O timeout, in seconds. |
| `CG_CONNECT_RETRIES` | Number of connection attempts, spaced about 0.2 seconds apart. |
| `TIMEOUT_S` | Controller timeout for an individual protocol run, in seconds. |

The thread counts above are the documented benchmark configuration. Reduce them
for a small smoke test on machines with fewer CPU cores; record any changes
when comparing timings. Multiple RF processes can run concurrently on a host.
The local-only `CG_RF_THREADS=2` from Section 3 is replaced by the controller's
per-host thread settings in these runs.

Start with small experiments:

```bash
CG_RF_THREADS_LOCAL=2 CG_RF_THREADS_REMOTE=2 bash run_2pc_direct_ole.sh 16
CG_RF_THREADS_LOCAL=2 CG_RF_THREADS_REMOTE=2 bash run_2pc_rf_psi.sh 16
```

`16` is the number of OLE instances in the first command and the size of each
PSI set in the second. The PSI default overlap is `floor(16/5)=3`. Each command
prints its output-directory path. Verify that `checks.csv` contains checker
status `0` and that both `local_rc` and `remote_rc` in `runs.csv` are `0`.
The scripts generate test inputs and run both hosts automatically.

## 7. Run individual protocols or the complete sweep

Each command below runs one protocol with benchmark size `N=1024`:

```bash
bash run_2pc_direct_ole.sh 1024
bash run_2pc_rf_ole.sh 1024
bash run_2pc_direct_ole3.sh 1024
bash run_2pc_rf_ole3.sh 1024
bash run_2pc_direct_ope.sh 1024
bash run_2pc_rf_ope.sh 1024
bash run_2pc_direct_opa.sh 1024
bash run_2pc_rf_opa.sh 1024
bash run_2pc_direct_psi.sh 1024
bash run_2pc_rf_psi.sh 1024
bash run_2pc_direct_psi2.sh 1024
bash run_2pc_rf_psi2.sh 1024
```

| Protocol | Meaning of `N` |
| --- | --- |
| Batched OLE and 3-round OLE | Number of OLE instances. |
| OPE | Sender polynomial degree; the reduction uses `N` OLE instances. |
| OPA | Number of public evaluation/OLE slots; `d=floor((N-2)/2)`. |
| One-way and two-way PSI | Number of elements in each party's set. |

PSI benchmark scripts generate overlap `floor(N/5)` by default. To choose a
different overlap and repeatable input-generation seed for an individual run:

```bash
OVERLAP=200 SEED=42 bash run_2pc_rf_psi.sh 1024
```

This generates two 1024-element sets with exactly 200 common elements.

Run all twelve variants for the smallest paper-table column:

```bash
bash benchmark_2pc_paper_all.sh 1024
```

Run the full six-column sweep:

```bash
export OUT_DIR="$PWD/benchmark_2pc_paper_all_$(date +%Y%m%d_%H%M%S)"
bash benchmark_2pc_paper_all.sh 1024 2048 4096 8192 16384 32768
python3 make_reported_total_table.py "$OUT_DIR" | tee "$OUT_DIR/reproduced_table.txt"
```

The positional arguments to the sweep are the benchmark sizes to run. With no
arguments, it uses the same six sizes. Choose a fresh `OUT_DIR` for each sweep;
`unset OUT_DIR` before returning to individual protocol commands so they create
their own directories. A full sweep can take hours, depending on the hardware.

RF two-way PSI uses a fixed reveal-slot count. An individual
`run_2pc_rf_psi2.sh N` run defaults to `N` slots. The full paper sweep uses the
public generated overlap as its reveal bound. To measure the conservative
`N`-slot setting explicitly for one size:

```bash
unset OUT_DIR
CG_PSI2_REVEAL_BOUND=1024 bash run_2pc_rf_psi2.sh 1024
```

Record the reveal bound with the results; different bounds change the amount
of protocol work.

## 8. Read and verify the results

| Output | Contents |
| --- | --- |
| `raw/` | Logs collected from both protocol hosts. |
| `io/` | Input/output files fetched for offline checks. |
| `runs.csv` | Process exit statuses and controller wall-clock times. |
| `protocol_times.csv` | Protocol times measured by each party, in milliseconds. |
| `timings.csv` | Detailed phase timings for individual runs. |
| `checks.csv` | Offline checker exit statuses and checker-log paths. |
| `run_config.txt` | Settings recorded for each individual protocol run. |

For an individual run, set `RUN_DIR` to the directory printed by its launcher:

```bash
export RUN_DIR="/absolute/path/printed/by/the/launcher"
cat "$RUN_DIR/runs.csv"
cat "$RUN_DIR/checks.csv"
cat "$RUN_DIR/protocol_times.csv"
```

Successful process and checker statuses are `0`. With `CG_SKIP_CHECKS=0`, a
`skipped` checker record is not an expected successful check. Sweep results
contain combined CSV files and a subdirectory for each protocol/size pair.

The reported protocol time is the maximum of the two party times. Input
generation, file loading, offline checker dumps, correctness checks, and
controller SSH overhead are excluded from this timer. Controller wall-clock
time is kept separately in `runs.csv`. The table generator reports seconds;
incomplete or failed measurements appear as `--`.

Compare the generated table with the paper only after matching the build,
hardware, input sizes, threads, lanes, chunk size, and reveal bound. The local
tests do not establish agreement with the published timing table.

## 9. Known two-way PSI padding limitation

Direct and RF two-way PSI pad unused reveal slots with `0`. If Party A contains
`0`, Party B does not, and a reveal slot is unused, Party A incorrectly includes
`0` in its output. For example, `A={0,1}`, `B={1,2}`, and two reveal slots give
Party A `{0,1}` instead of `{1}`. Party B's output is unaffected.

Exclude `0` from Party A's input to avoid this padding collision. A test that
reproduces this known fault is part of the automated suite; passing that test
means the documented behavior was observed, not that this fault was corrected.

## 10. Troubleshooting

- **SSH requests a password or reports permission denied:** repeat the two
  `BatchMode=yes` hostname checks in Section 4 and load an existing key into
  `ssh-agent` if it has a passphrase.
- **A binary is missing:** rebuild the source on the affected host and check
  `LOCAL_ROOT`, `REMOTE_ROOT`, `BUILD_DIR`, and `CHECKER_DIR`.
- **NTL is not found:** install `libntl-dev` on that host and rerun CMake.
- **Bind failure or a busy run lock:** inspect listeners with `ss -ltnp` and
  stop only your interrupted run, or wait for the current run to finish.
  Run the examples sequentially; they share default ports.
- **Connection timeout:** check host addresses, firewall rules, reachability of
  the configured protocol ports, and the role logs under `raw/`.
- **Checker failure or `--` timing:** inspect `checks.csv`, `runs.csv`, and the
  matching logs before treating the measurement as complete.

Press `Ctrl-C` in the controller terminal to stop a run. Launcher traps clean
up their own child processes. More manual role examples are in
[RUN_2PC_SETUP.md](RUN_2PC_SETUP.md).

## Source organization

- `cg_*.cpp`: protocol endpoints, reverse firewalls, and C++ checkers.
- `cg_*.hpp`: shared arithmetic, polynomial, networking, and timing helpers.
- `third_party/cg_ahe/`: CG-AHE wrapper and vendored BICYCL code.
- `run_*.sh`: localhost and two-machine launchers.
- `benchmark_2pc_paper_all.sh`: twelve-protocol, six-size benchmark driver.
- `generate_sets.py`, `generate_protocol_inputs.py`: test-input generators.
- `check_correctness.py`, `check_psi2_correctness.py`: PSI output checkers.
- `extract_timings.py`, `make_reported_total_table.py`: timing extraction and reporting.
- `scripts/`: shared launcher helpers and source packaging.
- `review/regression.*`, `review/check_direct_padding.py`: automated tests.

See [OPERATION_COMMENTS.md](OPERATION_COMMENTS.md) for the protocol-to-source
map, [RUN_TESTS.md](RUN_TESTS.md) for further local examples, and
[VALIDATION.md](VALIDATION.md) for completed validation.

## License

GPL-3.0-or-later. Vendored BICYCL code retains its original license notices.
