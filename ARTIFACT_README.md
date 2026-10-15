# Artifact Readme

This repository contains C++17 research proof-of-concept implementations of
CG-AHE protocols and reverse firewalls, with benchmark reproduction scripts.

Reported protocol families:

- Batched OLE and batched RF-OLE
- 3-round OLE and 3-round RF-OLE
- OPE and RF-OPE
- OPA and RF-OPA
- Direct one-way PSI and RF one-way PSI
- Direct two-way PSI and RF two-way PSI

Paper benchmark configuration:

- `CG_Q_NBITS=128`
- `CG_K=1`
- `CG_FIXED_Q=170141183460469232709364739622490341377`
- `CG_USE_NTT_POLY=1`
- `CG_BENCH_INPUT_BITS=128`
- `CG_PSI_INPUT_BITS=128`
- `CG_RF_LANES=8`
- `CG_RF_CHUNK_SIZE=128`
- `CG_RF_THREADS_LOCAL=28`
- `CG_RF_THREADS_REMOTE=32`
- `CG_RF_THREADS_PSI2_LOCAL=28`
- `CG_RF_THREADS_PSI2_REMOTE=32`
- reported protocol time is `max(local party time, remote party time)` from
  `protocol_times.csv`
- input generation, file loading, and offline correctness checks are excluded
  from the reported protocol timer

Run the benchmarks on a controlled private LAN with matching source versions
and environment settings. Protocol processes use TCP; the controller uses SSH
to launch processes and collect results. Leave `CG_VALIDATE_LANES` unset or `0`
for timing reproduction, or set it to `1` for extra configuration checks.

Direct two-way PSI sends zero padding in clear unused reveal slots; RF
two-way PSI encrypts the same zero padding.
Known correctness limitation: if Party A contains `0`, Party B does not,
and at least one reveal slot is unused, Party A falsely includes `0` in
its intersection output. Filtering against Party A's set does not remove
this collision. Party B's one-way output is unaffected. See `README.md`
for an example. This limitation applies to both two-way variants.

Repository layout:

- `cg_*.cpp`: protocol endpoints, firewalls, checkers, and small utilities.
- `cg_*.hpp`: shared networking, timing, polynomial, OLE/OPA/OPE/PSI, and
  reverse-firewall helpers.
- `run_*.sh`: localhost and one-protocol two-machine launchers.
- `benchmark_2pc_paper_all.sh`: full paper-table sweep.
- `RUN_2PC_SETUP.md`: two-machine setup, build, run, and troubleshooting steps.
- `OPERATION_COMMENTS.md`: role-by-role map from protocols to source files.

Run outputs:

- `raw/`: party logs.
- `io/`: fetched input/output files used by offline checkers.
- `runs.csv`: controller wall-clock time, including SSH/process overhead.
- `protocol_times.csv`: party-owned protocol timers used for reported times.
- `timings.csv`: parsed phase timings from raw logs.
- `checks.csv`: offline correctness checker status.

Build with NTL enabled on both protocol machines. The CMake configure output
should contain:

```text
Using NTL for large PSI polynomial operations: /usr/lib/.../libntl.so
```

The full paper sweep entrypoint is:

```bash
bash benchmark_2pc_paper_all.sh
```

Set `LOCAL_IP`, `REMOTE_IP`, `LOCAL_USER`, `REMOTE_USER`, `LOCAL_ROOT`,
`REMOTE_ROOT`, `BUILD_DIR`, and `CHECKER_DIR` before running it. See
`RUN_2PC_SETUP.md` for the complete setup and reproduction commands.

After a sweep finishes, reconstruct the paper-style table with:

```bash
python3 make_reported_total_table.py path/to/results
```

License:

This artifact is distributed under the GNU General Public License version 3 or
later. The vendored BICYCL code under `third_party/cg_ahe/bicycl/` retains its
original GPL-3.0-or-later notices.

Submission package:

```bash
python3 scripts/package_artifact.py
```

The command creates `dist/rf_class_groups_artifact.tar.gz` and its SHA-256
checksum. The archive includes source, vendored dependencies, documentation,
launchers, and regression tests. Local builds, measurement outputs, development
reports, and host-specific validation tools are excluded. Extract it into a
fresh directory, then follow the build and test commands in `README.md`.
The full timing sweep must be run separately on the evaluation machines.
