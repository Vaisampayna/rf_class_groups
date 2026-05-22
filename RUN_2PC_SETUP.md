# Running the 2-PC CG/RF Artifact from <controller-ip>

Use this machine, `<controller-ip>`, as the controller. The two protocol machines are:

- Receiver side: `<party-b-user>@<party-b-ip>`
- Sender side: `<party-a-user>@<party-a-ip>`

The controller starts both remote commands over SSH, collects logs locally, and runs offline checkers from `build-portable`.

## 1. Check SSH

Run these on `<controller-ip>`:

```bash
ssh -o BatchMode=yes <party-b-user>@<party-b-ip> 'hostname && pwd'
ssh -o BatchMode=yes <party-a-user>@<party-a-ip> 'hostname && pwd'
```

## 2. Sync the artifact to both protocol machines

Run from the controller copy:

```bash
cd <controller-workspace>

rsync -az --delete \
  --exclude build --exclude build-portable --exclude build-2pc \
  --exclude logs --exclude 'benchmark_2pc_*' \
  --exclude '*.pdf' --exclude '*.html' \
  reverse_firewall_cg/ \
  <party-b-user>@<party-b-ip>:<party-b-repo-path>/

rsync -az --delete \
  --exclude build --exclude build-portable --exclude build-2pc \
  --exclude logs --exclude 'benchmark_2pc_*' \
  --exclude '*.pdf' --exclude '*.html' \
  reverse_firewall_cg/ \
  <party-a-user>@<party-a-ip>:<party-a-repo-path>/
```

## 3. Build checker binaries on the controller

```bash
cd <controller-repo-path>
CG_BUILD_DIR="$PWD/build-portable" CMAKE_BUILD_PARALLEL_LEVEL=8 bash build_local.sh
```

## 4. Build protocol binaries separately on both protocol machines

For final benchmark runs, build directly on each protocol machine.  Both hosts
are Intel, but they are not the same CPU, so native instruction tuning must be
done per host.  Do not build once and copy the `build-2pc` directory to the
other machine.

The command below enables:

- `CG_ENABLE_NATIVE_ARCH=ON`: compile with `-march=native` on that machine.
- `CG_USE_NTL_POLY=ON`: use NTL for large PSI polynomial operations.
- `build-2pc`: the build directory used by all 2-PC run scripts.

```bash
ssh <party-b-user>@<party-b-ip> \
  'cd <party-b-repo-path> &&
   cmake -S . -B build-2pc -DCG_ENABLE_NATIVE_ARCH=ON -DCG_USE_NTL_POLY=ON &&
   cmake --build build-2pc -j2'

ssh <party-a-user>@<party-a-ip> \
  'cd <party-a-repo-path> &&
   cmake -S . -B build-2pc -DCG_ENABLE_NATIVE_ARCH=ON -DCG_USE_NTL_POLY=ON &&
   cmake --build build-2pc -j2'
```

The configure output should contain:

```text
Using NTL for large PSI polynomial operations: /usr/lib/x86_64-linux-gnu/libntl.so
```

If that line is missing on either host, install NTL on that host and rebuild:

```bash
sudo apt install libntl-dev
```

## 5. Quick smoke run

Prefer the per-protocol scripts for artifact testing. Each script below runs exactly one protocol and writes logs/checks under its own timestamped directory on `<controller-ip>`.

```bash
cd <controller-repo-path>

LOCAL_IP=<party-b-ip> \
REMOTE_IP=<party-a-ip> \
LOCAL_USER=<party-b-user> \
REMOTE_USER=<party-a-user> \
LOCAL_ROOT=<party-b-repo-path> \
REMOTE_ROOT=<party-a-repo-path> \
BUILD_DIR=build-2pc \
CHECKER_DIR="$PWD/build-portable" \
CG_Q_NBITS=128 \
CG_K=1 \
CG_BENCH_INPUT_BITS=128 \
CG_RF_LANES=8 \
CG_RF_THREADS_LOCAL=28 \
CG_RF_THREADS_REMOTE=32 \
CG_RF_CHUNK_SIZE=128 \
CG_CONNECT_RETRIES=7200 \
TIMEOUT_S=1800 \
bash run_2pc_rf_psi.sh 16
```

Available one-protocol scripts:

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

For two-way RF-PSI (`one-way RF-PSI + encrypted reveal-back`), the shortest
controller-side command is:

```bash
cd <controller-repo-path>
bash run_2pc_rf_psi2_reveal.sh 1000
```

Optional overrides:

```bash
OVERLAP=200 SEED=12345 CG_RF_LANES=8 \
bash run_2pc_rf_psi2_reveal.sh 1000
```

Each script uses input files under `/tmp/cg_*` on the protocol machines, then
runs an offline checker:

- OLE: `cg_check_batch_ole`
- OPE: `cg_check_ope`
- OPA: `cg_check_opa`
- one-way PSI: `check_correctness.py`
- two-way PSI: `check_psi2_correctness.py`

## 6. Full 2-PC sweep

```bash
cd <controller-repo-path>

LOCAL_IP=<party-b-ip> \
REMOTE_IP=<party-a-ip> \
LOCAL_USER=<party-b-user> \
REMOTE_USER=<party-a-user> \
LOCAL_ROOT=<party-b-repo-path> \
REMOTE_ROOT=<party-a-repo-path> \
BUILD_DIR=build-2pc \
CHECKER_DIR="$PWD/build-portable" \
CG_Q_NBITS=128 \
CG_K=1 \
CG_BENCH_INPUT_BITS=128 \
CG_RF_LANES=8 \
CG_RF_THREADS_LOCAL=28 \
CG_RF_THREADS_REMOTE=32 \
CG_RF_CHUNK_SIZE=128 \
CG_CONNECT_RETRIES=7200 \
TIMEOUT_S=7200 \
bash benchmark_2pc_cg_sweep.sh 100 1000 2000 5000 10000
```

The script prints the final `Logs: ...` path. Important files inside that directory:

- `runs.csv`: end-to-end controller wall-clock times.
- `timings.csv`: parsed internal phase timings from both parties.
- `checks.csv`: offline correctness checker status.
- `raw/`: raw receiver-side and sender-side process logs.
- `io/`: fetched input/output dumps used by the checkers.

## 7. If ports are stuck

Run this from `<controller-ip>` before restarting:

```bash
ssh <party-b-user>@<party-b-ip> \
  'fuser -k 9001/tcp 9002/tcp 9003/tcp 9004/tcp 9010/tcp 9041/tcp 9042/tcp 9043/tcp 9103/tcp 9104/tcp >/dev/null 2>&1 || true'

ssh <party-a-user>@<party-a-ip> \
  'fuser -k 9001/tcp 9002/tcp 9003/tcp 9004/tcp 9010/tcp 9041/tcp 9042/tcp 9043/tcp 9103/tcp 9104/tcp >/dev/null 2>&1 || true'
```

## 8. Stop a run midway

If a sweep or one-protocol run is still running and you want to stop it, press
`Ctrl-C` in the controller terminal first.  Then clean up both protocol
machines from `<controller-ip>`.

Kill CG protocol binaries and helper scripts on the receiver-side host:

```bash
ssh <party-b-user>@<party-b-ip> \
  'pkill -f "cg_.*receiver|cg_.*sender|cg_.*firewall|run_.*cg_|run_2pc_" >/dev/null 2>&1 || true;
   fuser -k 9001/tcp 9002/tcp 9003/tcp 9004/tcp 9010/tcp 9041/tcp 9042/tcp 9043/tcp 9103/tcp 9104/tcp >/dev/null 2>&1 || true'
```

Kill CG protocol binaries and helper scripts on the sender-side host:

```bash
ssh <party-a-user>@<party-a-ip> \
  'pkill -f "cg_.*receiver|cg_.*sender|cg_.*firewall|run_.*cg_|run_2pc_" >/dev/null 2>&1 || true;
   fuser -k 9001/tcp 9002/tcp 9003/tcp 9004/tcp 9010/tcp 9041/tcp 9042/tcp 9043/tcp 9103/tcp 9104/tcp >/dev/null 2>&1 || true'
```

If the controller-side sweep script is still alive locally, kill only the
benchmark launcher scripts:

```bash
pkill -f "benchmark_2pc_cg_sweep.sh|benchmark_2pc_cg_only.sh|run_2pc_" >/dev/null 2>&1 || true
```

## 9. Run One Protocol from Files

Use these when you do not want the full sweep.

Common environment used below:

```bash
export CG_Q_NBITS=128
export CG_K=1
export CG_BENCH_INPUT_BITS=128
export CG_RF_LANES=8
export CG_RF_CHUNK_SIZE=128
export CG_CONNECT_RETRIES=7200
```

### RF-OPA from coefficient files

Prepare coefficient files on the two protocol machines. Coefficients are in constant-term-first order.

Receiver side, `<party-b-ip>`, needs `pB.txt` with `m_B+1` coefficients:

```bash
ssh <party-b-user>@<party-b-ip> \
  'mkdir -p /tmp/cg_inputs && printf "1\n0\n1\n" > /tmp/cg_inputs/pB.txt'
```

Sender side, `<party-a-ip>`, needs `pA.txt` and `rA.txt`, each with `m_A+1` coefficients:

```bash
ssh <party-a-user>@<party-a-ip> \
  'mkdir -p /tmp/cg_inputs && printf "1\n2\n1\n" > /tmp/cg_inputs/pA.txt && printf "1\n1\n1\n" > /tmp/cg_inputs/rA.txt'
```

Run receiver side first, then sender side. This example uses `m_A=2`, `m_B=2`.

```bash
ssh <party-b-user>@<party-b-ip> \
  'cd <party-b-repo-path>/build-2pc;
   export CG_Q_NBITS=128 CG_K=1 CG_BENCH_INPUT_BITS=128 CG_RF_LANES=8 CG_RF_CHUNK_SIZE=128 CG_CONNECT_RETRIES=7200 CG_RF_THREADS=28;
   ./cg_rf_opa_receiver 2 2 --input-file /tmp/cg_inputs/pB.txt > ../logs/opa_receiver.log 2>&1 &
   ./cg_rf_receiver_firewall_opa > ../logs/opa_rf_receiver.log 2>&1;
   wait'
```

```bash
ssh <party-a-user>@<party-a-ip> \
  'cd <party-a-repo-path>/build-2pc;
   export CG_Q_NBITS=128 CG_K=1 CG_BENCH_INPUT_BITS=128 CG_RF_LANES=8 CG_RF_CHUNK_SIZE=128 CG_CONNECT_RETRIES=7200 CG_RF_THREADS=32;
   ./cg_rf_sender_firewall_opa <party-b-ip> > ../logs/opa_rf_sender.log 2>&1 &
   sleep 1;
   ./cg_rf_opa_sender 2 2 --input-file /tmp/cg_inputs/pA.txt -- --input-file /tmp/cg_inputs/rA.txt > ../logs/opa_sender.log 2>&1;
   wait'
```

### One-Way RF-PSI from set files

Receiver side has `set_B.txt`; sender side has `set_A.txt`. The receiver learns the intersection.

```bash
ssh <party-b-user>@<party-b-ip> \
  'mkdir -p /tmp/cg_inputs && printf "10\n20\n30\n40\n" > /tmp/cg_inputs/set_B.txt'

ssh <party-a-user>@<party-a-ip> \
  'mkdir -p /tmp/cg_inputs && printf "30\n40\n50\n60\n" > /tmp/cg_inputs/set_A.txt'
```

Here `m_A=4` and `m_B=4`.

```bash
ssh <party-b-user>@<party-b-ip> \
  'cd <party-b-repo-path>;
   CG_BUILD_DIR=$PWD/build-2pc CG_Q_NBITS=128 CG_K=1 CG_BENCH_INPUT_BITS=128 CG_RF_LANES=8 CG_RF_THREADS=28 CG_RF_CHUNK_SIZE=128 CG_CONNECT_RETRIES=7200 \
   bash ./run_rf_cg_psi_lan_receiver.sh --file /tmp/cg_inputs/set_B.txt 4 /tmp/cg_inputs/intersection_out.txt'
```

```bash
ssh <party-a-user>@<party-a-ip> \
  'cd <party-a-repo-path>;
   CG_BUILD_DIR=$PWD/build-2pc CG_Q_NBITS=128 CG_K=1 CG_BENCH_INPUT_BITS=128 CG_RF_LANES=8 CG_RF_THREADS=32 CG_RF_CHUNK_SIZE=128 CG_CONNECT_RETRIES=7200 \
   bash ./run_rf_cg_psi_lan_sender.sh <party-b-ip> --file /tmp/cg_inputs/set_A.txt 4'
```

Fetch the receiver output:

```bash
ssh <party-b-user>@<party-b-ip> 'cat /tmp/cg_inputs/intersection_out.txt'
```

### Exact Two-Way RF-PSI from set files

Both parties learn the intersection. Start Party B on `<party-b-ip>` first, then Party A on `<party-a-ip>`.

```bash
ssh <party-b-user>@<party-b-ip> \
  'mkdir -p /tmp/cg_inputs && printf "10\n20\n30\n40\n" > /tmp/cg_inputs/set_B.txt'

ssh <party-a-user>@<party-a-ip> \
  'mkdir -p /tmp/cg_inputs && printf "30\n40\n50\n60\n" > /tmp/cg_inputs/set_A.txt'
```

Here `m_A=4`, `m_B=4`.

```bash
ssh <party-b-user>@<party-b-ip> \
  'cd <party-b-repo-path>;
   CG_BUILD_DIR=$PWD/build-2pc CG_Q_NBITS=128 CG_K=1 CG_BENCH_INPUT_BITS=128 CG_RF_LANES=8 CG_RF_THREADS=10 CG_RF_CHUNK_SIZE=128 CG_CONNECT_RETRIES=7200 \
   bash ./run_rf_cg_psi2_lan_b.sh <party-a-ip> --file 4 /tmp/cg_inputs/set_B.txt'
```

```bash
ssh <party-a-user>@<party-a-ip> \
  'cd <party-a-repo-path>;
   CG_BUILD_DIR=$PWD/build-2pc CG_Q_NBITS=128 CG_K=1 CG_BENCH_INPUT_BITS=128 CG_RF_LANES=8 CG_RF_THREADS=12 CG_RF_CHUNK_SIZE=128 CG_CONNECT_RETRIES=7200 \
   bash ./run_rf_cg_psi2_lan_a.sh <party-b-ip> --file /tmp/cg_inputs/set_A.txt 4'
```

The two-way logs are:

```bash
ssh <party-b-user>@<party-b-ip> 'cat <party-b-repo-path>/logs/psi2_B.log'
ssh <party-a-user>@<party-a-ip> 'cat <party-a-repo-path>/logs/psi2_A.log'
```
