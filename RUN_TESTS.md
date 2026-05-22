# Running Custom Tests for RF-PSI, RF-OPA, and RF-OLE

This document explains how to execute the custom benchmarking and testing scripts for the Class Group Additively Homomorphic Encryption (CG-AHE) Reverse Firewall implementations.

All commands below should be run from within the `reverse_firewall_cg` directory.

For the batched RF-OLE, RF-OPA, and RF-PSI scripts, set `CG_RF_LANES=N` to open
`N` parallel TCP connections per hop. If unset, the demos use one connection.
For example:

```bash
CG_RF_LANES=4 bash run_rf_cg_psi_local.sh "10 20 30" "20 30 40"
```

## 1. Running Private Set Intersection (RF-PSI)
The `run_rf_cg_psi_local.sh` script tests the complete Private Set Intersection protocol. It requires exactly two arguments: the Sender's set and the Receiver's set, passed as space-separated strings.

**Test small, specific sets:**
```bash
bash run_rf_cg_psi_local.sh "8 19 42 100 500" "42 99 100 101"
```

**Test large generated sets (e.g. 100 elements each):**
```bash
bash run_rf_cg_psi_local.sh "$(seq 1 100 | tr '\n' ' ')" "$(seq 50 149 | tr '\n' ' ')"
```

**Test unequal sets:**
```bash
bash run_rf_cg_psi_local.sh "$(seq 1 200 | tr '\n' ' ')" "50 100 150 250 300 400 500"
```

---

## 2. Running Oblivious Polynomial Evaluation (RF-OPA)
The `run_rf_cg_opa_local.sh` script tests the underlying polynomial evaluation.
It requires the degrees `m_A` and `m_B`, followed by the exact polynomial coefficients for `pA`, `rA` (Sender polynomials), and `pB` (Receiver polynomial).
The number of coefficients for `pA` and `rA` must be exactly `m_A+1`.
The number of coefficients for `pB` must be exactly `m_B+1`.

**Test a tiny polynomial (degrees m_A=2, m_B=2):**
```bash
# p_A(X) = 1 + 2X + X^2  --> 1 2 1
# r_A(X) = 1 + X + X^2   --> 1 1 1
# p_B(X) = -1 + X^2      --> -1 0 1
bash run_rf_cg_opa_local.sh 2 2 "1 2 1" "1 1 1" "-1 0 1"
```

**Test a massive polynomial (degrees m_A=500, m_B=500) and track time:**
*(Generates 501 sequential coefficients for each polynomial)*
```bash
PA=$(seq 1 501 | tr '\n' ' ')
RA=$(seq 1 501 | tr '\n' ' ')
PB=$(seq 1 501 | tr '\n' ' ')
time bash run_rf_cg_opa_local.sh 500 500 "$PA" "$RA" "$PB"
```

**Test unequal polynomials (m_A=200, m_B=10):**
```bash
PA=$(seq 1 201 | tr '\n' ' ')
RA=$(seq 1 201 | tr '\n' ' ')
PB=$(seq 1 11 | tr '\n' ' ')
time bash run_rf_cg_opa_local.sh 200 10 "$PA" "$RA" "$PB"
```

---

## 3. Running Oblivious Linear Evaluation (RF-OLE)
The `run_rf_cg_ole_local.sh` script tests the foundational OLE primitive: `y = A * X + B`.
It takes three space-separated strings of numbers representing the sequences of `A`, `B`, and `X`. All three sequences must be the same length.

**Test a small batch of OLEs:**
```bash
bash run_rf_cg_ole_local.sh "1 2 3" "10 20 30" "5 5 5"
```

**Test a large batch (100 parallel OLE operations):**
```bash
A=$(seq 1 100 | tr '\n' ' ')
B=$(seq 101 200 | tr '\n' ' ')
X=$(seq 201 300 | tr '\n' ' ')
time bash run_rf_cg_ole_local.sh "$A" "$B" "$X"
```

---

### Helpful Tips
- **Logs:** While any test is running in the background, you can monitor the live progress of all four nodes (Sender, S-RF, R-RF, Receiver) by running `tail -f logs/*.log` in a separate terminal.
- **Precomputations:** For large sets, the scripts will spend significant time in the `[OFFLINE PHASE]`. The pipeline will pause briefly while the firewalls precompute their randomizers before the `[ONLINE PHASE]` begins streaming data.
