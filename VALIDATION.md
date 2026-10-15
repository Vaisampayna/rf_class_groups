# Validation

## Local builds and tests

The application was built and tested on 2026-10-15 with GCC 15.2, CMake,
Python 3.14, and the built-in polynomial backend. Both Debug and clean Release
builds passed the protocol tests. The Release build enabled LTO. NTL was not
installed on the local test host.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCG_USE_NTL_POLY=ON
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

When NTL is unavailable, CMake reports a fallback to the built-in backend.
The local test results cover that backend.

- `cg_regressions`: input parsing, sampling, integer encoding, TCP exchange,
  parameter mismatches, lane and worker failures, timing completeness,
  controller CSV formatting, protocol components, and local launchers.
- `cg_direct_padding`: ordinary intersections, zero as an intersection element,
  unused reveal slots, and the documented zero-padding collision.

The standalone CG-AHE tests passed in Release mode with assertions enabled:

```bash
cmake -S third_party/cg_ahe -B build-ahe -DCMAKE_BUILD_TYPE=Release
cmake --build build-ahe -j2
./build-ahe/test_cg_ahe
```

They exercise encryption/decryption, homomorphic addition and scalar
multiplication, rerandomization, and paired key/ciphertext mauling.

## Two-machine RF two-way PSI

A six-size run completed for `N=1024,2048,4096,8192,16384,32768` with
28 receiver-side and 32 sender-side workers per process. Both process exit
statuses and both-party correctness checks succeeded for every size. The
recorded reveal bound was `floor(N/5)`.

This run validates the generated test cases. It does not remove the known
zero-padding fault described in README Section 9.

## Timing interpretation

RF two-way PSI records each party's one-way phase time plus its reveal phase
time, then reports the maximum of those two sums. The phase timers start at
different moments and may include waiting for peer initialization. They are
not a continuous clock from a common start through Party A's final completion.
A completion acknowledgment and a defined start boundary are needed for that
measurement. Controller wall-clock time is recorded separately in `runs.csv`.

The timing results should be compared with the paper only under a matching
measurement convention, build, hardware, and configuration.

## Source checks

Python files, shell scripts, documented shell examples, local documentation
links, and application include paths were checked. The Git upload file list
was compared with the source archive manifest. The archive checksum was
verified, and `git diff --check` passed.

Tests are stored in `tests/`. Generated builds, logs, input files, and local
experiment outputs are excluded from the source package. License and copyright
notices are retained. These functional tests are not a cryptographic security
proof.
