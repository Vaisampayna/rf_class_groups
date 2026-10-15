# Validation

Final pre-upload checks completed on 2026-10-15 using GCC 15.2, CMake, Python
3.14, and the built-in polynomial backend. NTL is not installed on this host.

## Build and protocol tests

The existing development build passed both CTest suites:

```bash
cmake -S . -B build-review -DCG_ENABLE_LTO=OFF -DCMAKE_BUILD_TYPE=Debug
cmake --build build-review -j2
ctest --test-dir build-review --output-on-failure
```

A separate clean source extraction was configured in Release mode with LTO
and built successfully. Both CTest suites passed in that build as well:

```bash
cmake -S . -B build-final -DCMAKE_BUILD_TYPE=Release -DCG_USE_NTL_POLY=ON
cmake --build build-final -j2
ctest --test-dir build-final --output-on-failure
```

Although NTL was requested, CMake reported that it was unavailable and used
the built-in backend. These results therefore do not validate the NTL backend.

- `cg_regressions`: strict input parsing, sampling, integer encoding, TCP
  exchange, optional parameter mismatches, lane and worker failure propagation,
  timing completeness, direct/RF protocol components, and local launchers.
  A controller test verifies that the two log paths occupy one correctly quoted
  field in `runs.csv`.
- `cg_direct_padding`: ordinary intersection, the known zero-padding collision,
  zero as a real intersection element, and no unused reveal slots.

## Standalone CG-AHE tests

The standalone wrapper tests also built and passed in Release mode with
assertions enabled:

```bash
cmake -S third_party/cg_ahe -B build-ahe -DCMAKE_BUILD_TYPE=Release
cmake --build build-ahe -j2
./build-ahe/test_cg_ahe
```

They exercise encryption/decryption, homomorphic addition and scalar
multiplication, rerandomization, and paired key/ciphertext mauling.

## Source and packaging checks

All 145 prospective Git upload files were checked. The checks covered Python
syntax, shell-script syntax, shell examples in the Markdown files, local links,
application include paths, and agreement between Git's upload file list and
the source archive manifest. No private-key or common access-token patterns
were found in these files or the existing Git history.

Local builds, measurements, machine-specific development helpers, and development
reports are excluded. The archive contents match the source files, and its
SHA-256 checksum was verified. `git diff --check` passed.

The final review corrected stale setup instructions and log paths, corrected the
controller CSV quoting, and kept standalone test assertions active in Release
mode. The complete reviewer command sequence is in `README.md`.

RF two-way PSI launchers now default to 28 receiver-side and 32 sender-side
workers per process. The default environment, recorded settings, and shell
syntax were checked after this update. The protocol tests above use explicit
small worker counts; a complete two-host 28/32 benchmark remains pending.

## Limits

These checks do not reproduce the published timing table or provide a security
proof. Rebuild the submitted source on both protocol hosts and run the documented
sweep to validate NTL-backed execution and timing agreement. The known two-way
PSI zero-padding fault remains documented in README Section 9; its regression
test verifies the observed limitation, rather than correcting it.
