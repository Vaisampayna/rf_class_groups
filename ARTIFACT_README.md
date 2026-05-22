# Anonymous Artifact

This artifact contains the implementation used for the reported CG-AHE
reverse-firewall experiments.  The benchmark configuration used for the paper
is:

Reported rows:

- Batched OLE and batched RF-OLE
- 3-round OLE and 3-round RF-OLE
- OPE and RF-OPE
- OPA and RF-OPA
- Direct one-way PSI and RF one-way PSI
- Direct two-way PSI and RF two-way PSI

- `CG_Q_NBITS=128`
- `CG_K=1`
- `CG_BENCH_INPUT_BITS=128`
- PSI elements sampled over the 128-bit plaintext field `Z_q`
- reported protocol time is `max(local party time, remote party time)`
- input generation/loading and offline correctness checker dumps are excluded

For two-machine runs, replace the placeholders below with the two hostnames or
IP addresses available in the review environment:

```bash
LOCAL_IP=<party_b_ip>
REMOTE_IP=<party_a_ip>
LOCAL_USER=<party_b_user>
REMOTE_USER=<party_a_user>
LOCAL_ROOT=<party_b_repo_path>
REMOTE_ROOT=<party_a_repo_path>
BUILD_DIR=build-2pc
```

The helper `make_reported_total_table.py` reconstructs the table values from
raw benchmark logs when those logs are available.  The raw logs are not included
in this anonymous artifact because they contain machine-specific paths.
