# Artifact Readme

This repository contains the C++17 implementation used for the CG-AHE
reverse-firewall experiments in the paper.

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
- reported protocol time is `max(local party time, remote party time)` from
  `protocol_times.csv`
- input generation, file loading, and offline correctness checks are excluded
  from the reported protocol timer

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
python3 make_reported_total_table.py <benchmark_2pc_paper_all_...>
```
