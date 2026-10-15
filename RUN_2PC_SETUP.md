# Two-machine setup

The complete command sequence is in [README.md](README.md):

- Section 4: machine variables, passwordless SSH, dependencies, and source copying.
- Section 5: native builds on both protocol hosts and portable controller checkers.
- Section 6: runtime settings and small two-machine smoke tests.
- Section 7: all twelve protocols and the six-size sweep.
- Section 8: output files and verification.
- Section 10: connection, build, and port troubleshooting.

Run the controller commands from one shell so exported variables remain set.
`LOCAL_*` identifies Party B (receiver), and `REMOTE_*` identifies Party A
(sender). The controller may be either host or a separate machine. SSH access
is required from the controller to both hosts; protocol hosts communicate over
TCP and do not need SSH access to each other.

## Entry points

| Script | Purpose |
| --- | --- |
| `run_2pc_direct_ole.sh`, `run_2pc_rf_ole.sh` | One batched OLE experiment. |
| `run_2pc_direct_ole3.sh`, `run_2pc_rf_ole3.sh` | One 3-round OLE experiment. |
| `run_2pc_direct_ope.sh`, `run_2pc_rf_ope.sh` | One OPE experiment. |
| `run_2pc_direct_opa.sh`, `run_2pc_rf_opa.sh` | One OPA experiment. |
| `run_2pc_direct_psi.sh`, `run_2pc_rf_psi.sh` | One one-way PSI experiment. |
| `run_2pc_direct_psi2.sh`, `run_2pc_rf_psi2.sh` | One two-way PSI experiment. |
| `benchmark_2pc_paper_all.sh` | All twelve protocols over the requested sizes. |
| `run_2pc_direct_psi2_sweep.sh`, `run_2pc_rf_psi2_sweep.sh` | Two-way PSI only, over the requested sizes. |

Individual protocol launchers take one benchmark size `N`; sweep scripts take
one or more sizes. Size meanings are explained in README Section 7.
`benchmark_2pc_cg_sweep.sh` is a compatibility alias for the full paper driver.

`run_2pc_rf_psi2_reveal.sh` is an optional wrapper around `run_2pc_rf_psi2.sh`.
It selects the public generated overlap as the RF reveal-slot bound. The main
single-run launcher instead defaults to `N` slots. Both use the same `28/32`
receiver/sender RF PSI2 thread defaults unless overridden. Use the main launcher
or full paper driver for the command sequence in README.

## Runtime notes

- Use matching source versions, scheme parameters, lanes, and chunk sizes on
  both hosts. `CG_VALIDATE_LANES=1` enables optional configuration checks;
  the documented timing setup uses `0`.
- `CG_BIND_ADDR` selects the listener interface; LAN launchers default to
  `0.0.0.0`. `CG_IO_TIMEOUT_S` defaults to 180 seconds.
- Run experiments sequentially with the default port map. For concurrent runs,
  use disjoint ports and `CG_RUN_LOCK` paths.
- Each individual run records settings in `run_config.txt` and writes logs under
  `raw/`. RF PSI2 output files fetched by the checker are in `io/<run>/`.
- Use a fresh `OUT_DIR` for each experiment. Do not reuse a previous sweep's
  directory. Unset `OUT_DIR` after a sweep before starting individual runs.
- PSI files must contain unique elements in `[0,q-1]`. Both two-way variants
  have the zero-padding correctness limitation documented in README Section 9.

The `run_rf_cg_*_lan_*.sh` scripts are lower-level entry points for manually
starting one host's roles. Controller launchers supply the matching parameters,
inputs, and start order automatically; use them for the reviewer workflow.
