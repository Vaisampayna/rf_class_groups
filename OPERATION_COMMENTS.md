# reverse_firewall_cg Operation Comments

This file describes the source files used by the reported CG-AHE
reverse-firewall experiments. Generated build directories, benchmark logs, and
temporary input files are intentionally omitted.

## Reported Protocol Rows

The artifact supports the following benchmark rows:

- Batched OLE and batched RF-OLE.
- 3-round OLE and 3-round RF-OLE.
- OPE and RF-OPE.
- OPA and RF-OPA.
- Direct one-way PSI and RF one-way PSI.
- Direct two-way PSI and RF two-way PSI.

## Shared Components

- `cg_common.hpp`: socket helpers and CG-AHE setup for the four-process
  batched RF-OLE path.
- `cg_network.hpp`: length-prefixed TCP serialization helpers for the
  OLE/OPE/OPA/PSI benchmark binaries.
- `cg_rf_common.hpp`: shared benchmark utilities, RF mauling/rerandomization
  helpers, polynomial operations, NTL-backed large-polynomial paths, input-file
  parsing, and protocol timing helpers.
- `cg_thread_pool.hpp`: fixed-size thread pool used by batched encryption,
  decryption, mauling, and polynomial evaluation helpers.
- `cg_ope_common.hpp`: direct batched OLE transport and Horner-to-OLE utilities
  used by OPE, OPA, and direct PSI.
- `cg_ole3_common.hpp`: shared direct and RF 3-round OLE helpers.
- `cg_opa.hpp`: direct OPA reduction to direct batched OLE.
- `cg_rf_ole_batch.hpp`: batched RF-OLE endpoint helper used by RF-OPA,
  RF-OPE, RF-PSI, and RF two-way PSI.
- `cg_rf_opa.hpp`: RF-OPA reduction to batched RF-OLE.

## Batched OLE

- `cg_batch_ole_receiver.cpp` and `cg_batch_ole_sender.cpp` run direct batched
  OLE. The receiver reads or samples the OLE receiver vector `x`; the sender
  reads or samples vectors `a,b`; the receiver writes decrypted values
  `a_i*x_i+b_i`.
- `cg_rf_batch_ole_receiver.cpp`, `cg_rf_batch_ole_sender.cpp`,
  `cg_receiver.cpp`, `cg_rrf.cpp`, `cg_srf.cpp`, and `cg_sender.cpp` implement
  the four-process batched RF-OLE topology:

```text
Receiver -> R-RF -> S-RF -> Sender
Receiver <- R-RF <- S-RF <- Sender
```

The receiver encrypts each `x_i`; R-RF and S-RF maul and rerandomize the
ciphertexts before the sender sees them; the sender computes encrypted
`a_i*x_i+b_i`; S-RF and R-RF align, unmaul, and rerandomize the response before
the receiver decrypts it.

## 3-Round OLE

- `cg_ole3_receiver.cpp` and `cg_ole3_sender.cpp` implement direct 3-round OLE.
- `cg_rf_ole3_receiver.cpp`, `cg_rf_ole3_sender.cpp`,
  `cg_rf_receiver_firewall_ole3.cpp`, and `cg_rf_sender_firewall_ole3.cpp`
  implement RF 3-round OLE.

## OPE

- `cg_ope_receiver.cpp` and `cg_ope_sender.cpp` implement direct OPE by turning
  Horner evaluation into a batch of direct OLE calls.
- `cg_rf_ope_receiver.cpp`, `cg_rf_ope_sender.cpp`,
  `cg_rf_receiver_firewall_opa.cpp`, and `cg_rf_sender_firewall_opa.cpp`
  implement RF-OPE through the RF-OPA/RF-OLE path.

## OPA

- `cg_opa_receiver.cpp` and `cg_opa_sender.cpp` implement direct OPA by
  evaluating the receiver polynomial and the sender polynomials at public
  points and using direct batched OLE.
- `cg_rf_opa_receiver.cpp`, `cg_rf_opa_sender.cpp`,
  `cg_rf_receiver_firewall_opa.cpp`, and `cg_rf_sender_firewall_opa.cpp`
  implement RF-OPA through batched RF-OLE.

## PSI

- `cg_psi_receiver.cpp` and `cg_psi_sender.cpp` implement direct one-way PSI.
  The sender builds `p_A`, samples `r_A` and `r'_A`, forms
  `q_A = p_A * r'_A`, and invokes one OPA call so the receiver obtains
  `q_A + r_A*p_B` and outputs the zero evaluations on `S_B`.
- `cg_rf_psi_receiver.cpp` and `cg_rf_psi_sender.cpp` implement RF one-way PSI
  using the same PSI polynomial relation through RF-OPA.
- `cg_psi2_receiver.cpp` and `cg_psi2_sender.cpp` implement direct two-way PSI:
  the receiver first computes the direct one-way PSI result, then sends the
  intersection back to the sender on a direct reveal channel.
- `cg_rf_psi2_reveal_receiver.cpp`, `cg_rf_psi2_reveal_sender.cpp`,
  `cg_rf_psi2_reveal_rrf.cpp`, and `cg_rf_psi2_reveal_srf.cpp` implement RF
  two-way PSI: one-way RF-PSI followed by an encrypted reveal-back phase through
  the two firewalls.

## Input And Output Files

- `generate_protocol_inputs.py` creates file-backed OLE/OPE/OPA inputs.
- `generate_sets.py` creates PSI sets and `true_intersection.txt`; the 2PC PSI
  scripts pass the plaintext-field bound so sampled elements lie in `Z_q`.
- Protocol binaries write party timing files such as `protocol_time.csv`,
  `protocol_time_A.csv`, or `protocol_time_B.csv` in each run's I/O directory.
- PSI receivers write `intersection_out.txt`; direct two-way PSI senders write
  `sender_intersection_out.txt`.
- `check_correctness.py` and `check_psi2_correctness.py` validate generated
  outputs after the protocol processes exit.

## Launcher Scripts

- `run_2pc_direct_ole.sh`, `run_2pc_rf_ole.sh`, `run_2pc_direct_ope.sh`,
  `run_2pc_rf_ope.sh`, `run_2pc_direct_opa.sh`, `run_2pc_rf_opa.sh`,
  `run_2pc_direct_psi.sh`, `run_2pc_rf_psi.sh`, `run_2pc_direct_psi2.sh`, and
  `run_2pc_rf_psi2.sh` run one two-machine experiment each.
- `benchmark_2pc_cg_sweep.sh`, `benchmark_2pc_ole3_sweep.sh`,
  `run_2pc_direct_psi2_sweep.sh`, and `run_2pc_rf_psi2_sweep.sh` run the
  table-style sweeps.
- `make_reported_total_table.py` reconstructs the total-time table from raw
  benchmark logs when those logs are available.
