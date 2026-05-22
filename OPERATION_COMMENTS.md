# reverse_firewall_cg Operation Comments

This file is a readable "which code does what" guide for the
`reverse_firewall_cg` folder. It intentionally skips generated files under
`build/` and runtime logs under `logs/`.

## Active CMake Targets

The active build in `CMakeLists.txt` compiles these five programs:

- `cg_receiver.cpp`: receiver endpoint for batched RF-OLE.
- `cg_rrf.cpp`: receiver-side reverse firewall for batched RF-OLE.
- `cg_srf.cpp`: sender-side reverse firewall for batched RF-OLE.
- `cg_sender.cpp`: sender endpoint for batched RF-OLE.
- `cg_bench.cpp`: standalone benchmark/simulation without sockets.

Several `cg_rf_*` OLE/OPA/PSI files are present as demo variants, but they are
not currently included in `CMakeLists.txt`.

## Protocol Topology

The main four-process RF-OLE topology is:

```text
Receiver -> R-RF -> S-RF -> Sender
Receiver <- R-RF <- S-RF <- Sender
```

Round 1 sends encrypted receiver inputs toward the sender:

```text
Receiver creates pk, sk and Enc_pk(x_i)
R-RF mauls pk to pk' and converts Enc_pk(x_i) into Enc_pk'(x_i)
R-RF rerandomizes the ciphertext
S-RF mauls pk' to fpk and converts Enc_pk'(x_i) into Enc_fpk(x_i)
S-RF rerandomizes the fpk ciphertext
Sender receives fpk and Enc_fpk(x_i)
```

Round 2 sends encrypted sender results back:

```text
Sender computes Enc_fpk(a_i*x_i + b_i)
S-RF aligns the output back to pk' and rerandomizes it
R-RF unmauls it back under the receiver key pk and rerandomizes it
Receiver decrypts and obtains a_i*x_i + b_i
```

## `cg_common.hpp`

- Header guard and includes: enable POSIX socket APIs, C/C++ utilities, BICYCL,
  and the local CG-AHE wrapper.
- `using namespace BICYCL; using namespace CG_AHE;`: lets the active files use
  `Mpz`, `QFI`, `CG_Scheme`, `CipherText`, etc. without namespace prefixes.
- `wall_now_s()`: reads `CLOCK_MONOTONIC` and returns seconds as a `double`.
- `die()` / `die_errno()`: print a role-tagged fatal error and terminate.
- `send_all()`: repeatedly calls `send()` until all requested bytes are written.
- `recv_all()`: repeatedly calls `recv()` until all requested bytes are read.
- `send_u32()` / `recv_u32()`: transfer 32-bit integers in network byte order.
- `send_u64()` / `recv_u64()`: transfer 64-bit integers as two network-order
  32-bit words.
- `send_mpz()`: serializes a BICYCL `Mpz` into a byte vector and sends length
  followed by bytes.
- `recv_mpz()`: receives that length-prefixed byte representation and rebuilds
  an `Mpz`.
- `send_qfi()` / `recv_qfi()`: transfer a class-group form by sending its
  `a`, `b`, and `c` multiprecision components.
- `send_ct()` / `recv_ct()`: transfer a CG-AHE ciphertext by sending its two
  `QFI` components.
- `send_pk()` / `recv_pk()`: transfer a public key by sending its class-group
  element and rebuilding it inside a cryptosystem context.
- `seed_randgen()`: seeds BICYCL's random generator from `/dev/urandom`, with a
  time-based fallback if the device read fails.
- `set_sockopts()`: enables `TCP_NODELAY` on accepted/connected sockets.
- `listen_tcp()`: resolves a local listening address, creates a socket, binds,
  and listens.
- `accept_tcp()`: accepts one incoming TCP connection and applies socket options.
- `connect_tcp()`: repeatedly tries to connect to a host/port until successful
  or the retry window expires.
- `mix32()`: deterministic integer mixer used for repeatable benchmark inputs.
- `sample16()`: derives a reproducible 16-bit sample from a seed and index.

## `cg_receiver.cpp`

- Parses `rrf_host`, `rrf_port`, `n_oles`, and `repeat` from the command line.
- Seeds randomness and constructs the CG-AHE scheme.
- Generates the receiver secret key and public key.
- Connects to the receiver-side reverse firewall.
- Sends the batch size and receiver public key to R-RF.
- For each OLE index:
  - derives deterministic benchmark input `x_i`;
  - encrypts `x_i` under the receiver public key;
  - sends `Enc_pk(x_i)` to R-RF.
- For each OLE index:
  - receives the returned encrypted result;
  - decrypts it with the receiver secret key;
  - stores the clear result.
- Sends a verification token back through the chain.
- Prints CSV timing data for setup, key generation, encryption, decryption, and
  wall-clock time.

## `cg_rrf.cpp`

- Parses the receiver listen port plus S-RF host/port.
- Constructs the CG-AHE scheme used for class-group operations.
- Listens for the receiver and connects onward to S-RF.
- Receives the receiver public key and OLE count.
- Samples one mauling secret `r` for the session.
- Computes `pk' = pk * h^r`, which hides the receiver's real public key from the
  sender side.
- Sends the batch count and mauled public key `pk'` to S-RF.
- For each incoming receiver ciphertext:
  - receives `Enc_pk(x_i)`;
  - computes `R_i^r`;
  - multiplies the ciphertext's second component by `R_i^r`, converting it to
    an encryption under `pk'`;
  - adds fresh encryption randomness under `pk'`;
  - forwards the rerandomized ciphertext to S-RF.
- For each returned sender result:
  - receives a result that S-RF has already aligned back under `pk'`;
  - computes `R_y^r`;
  - divides the ciphertext's second component by `R_y^r`;
  - rerandomizes under the original receiver key `pk`;
  - forwards the refreshed ciphertext under the original receiver key to the
    receiver.
- Relays the final verification token from receiver to S-RF.

## `cg_srf.cpp`

- Parses the R-RF listen port plus sender host/port.
- Constructs the CG-AHE scheme.
- Listens for R-RF and connects to the sender.
- Receives the batch count and mauled public key `pk'` from R-RF.
- Samples sender-firewall mauling secret `rho`.
- Computes `fpk = pk' * h^rho`.
- Forwards the batch count and `fpk` to the sender.
- For each Round 1 ciphertext:
  - receives a ciphertext from R-RF;
  - multiplies its second component by `c1^rho`, aligning it from `pk'` to
    `fpk`;
  - samples fresh rerandomization randomness;
  - builds an encryption of zero under `fpk`;
  - homomorphically adds it to the incoming ciphertext;
  - forwards the rerandomized ciphertext to the sender.
- For each Round 2 ciphertext:
  - receives the sender's encrypted result under `fpk`;
  - divides its second component by `c1^rho`, aligning it back to `pk'`;
  - rerandomizes it under `pk'`;
  - sends it back to R-RF.
- Relays the verification token from R-RF to the sender.

## `cg_sender.cpp`

- Parses S-RF port, OLE count, and repeat number.
- Constructs the CG-AHE scheme.
- Listens for the sender-side reverse firewall.
- Receives the expected OLE count and sender-firewall-mauled public key `fpk`.
- For each OLE index:
  - receives `Enc_fpk(x_i)`;
  - derives deterministic benchmark values `a_i` and `b_i`;
  - computes homomorphic scalar multiplication by `a_i`;
  - encrypts `b_i` under `fpk`;
  - homomorphically adds both pieces to form `Enc_fpk(a_i*x_i+b_i)`;
  - sends the result back to S-RF.
- Receives the verification token through the firewall chain.
- Prints sender-side CSV timing data.

## `cg_bench.cpp`

- Parses the benchmark iteration count.
- Constructs a CG-AHE scheme and generates a reusable key pair.
- Prepares sample plaintexts, ciphertexts, scalar values, and mauling randomness.
- Benchmarks individual operations:
  - key generation;
  - encryption;
  - decryption;
  - ciphertext scalar multiplication;
  - homomorphic addition;
  - rerandomization;
  - public-key mauling;
  - ciphertext mauling;
  - ciphertext unmauling;
  - sender-side OLE evaluation.
- Simulates full RF-OLE batches for sizes 1 through 128 without sockets.
- In each simulated OLE:
  - receiver encrypts `x_i`;
  - R-RF mauls and rerandomizes;
  - S-RF mauls to `fpk` and rerandomizes;
  - sender computes encrypted `a_i*x_i+b_i` under `fpk`;
  - S-RF aligns back to `pk'` and rerandomizes the output;
  - R-RF unmauls and rerandomizes under `pk`;
  - receiver decrypts.
- Verifies one sample result against the cleartext formula.
- Prints timing tables and CSV-style batch rows.

## `cg_rf_common.hpp`

- Defines shared CG-AHE parameters: plaintext modulus size, `k`, and security
  level.
- Defines localhost ports used by the older `cg_rf_*` demo programs.
- Provides `ms_since()` for millisecond timing.
- `maul_ct_forward()`: converts a ciphertext from `pk` to `pk'` by multiplying
  the second component by `c1^r`.
- `maul_ct_inverse()`: converts a ciphertext from `pk'` back to `pk` by removing
  the `c1^r` factor.
- `maul_pk()`: computes the mauled public key `pk' = pk * h^r`.
- `poly_from_roots()`: builds a polynomial whose roots are the provided set
  elements.
- `poly_eval()`: evaluates a polynomial modulo the plaintext modulus.
- `lagrange_eval()`: evaluates the interpolating polynomial at a point using
  Lagrange interpolation modulo the plaintext modulus.
- `random_poly()`: samples random polynomial coefficients modulo the plaintext
  modulus.

## `cg_network.hpp`

- Contains an alternate networking/serialization helper namespace `CGNet`.
- `send_all()` / `recv_all()`: write/read exact byte counts using `write()` and
  `read()`.
- `send_u32()` / `recv_u32()`: send/receive network-order 32-bit integers.
- `send_u64()` / `recv_u64()`: send/receive 64-bit integers as two 32-bit
  pieces.
- `connect_tcp_retry()`: repeatedly tries to connect to a host and port.
- `listen_tcp()`: creates, binds, and listens on a TCP socket.
- `accept_one()`: accepts a single TCP client.
- `send_mpz()` / `recv_mpz()`: send/receive signed GMP/BICYCL integers.
- `send_qfi()` / `recv_qfi()`: serialize class-group forms.
- `send_pk()` / `recv_pk()`: serialize CG-AHE public keys.
- `send_ct()` / `recv_ct()`: serialize CG-AHE ciphertexts.

Note: `cg_common.hpp` and `cg_network.hpp` use different `Mpz` wire formats.
Do not mix programs using one helper with programs using the other unless the
serialization is made consistent.

## Older Single-OLE Demo Files

### `cg_rf_receiver.cpp`

- Parses receiver input `x`.
- Builds a CG-AHE scheme, key pair, and encryption of `x`.
- Listens for R-RF on the receiver port.
- Sends `(pk, Enc_pk(x))` to R-RF.
- Receives the encrypted result from R-RF.
- Decrypts and prints `a*x+b`.

### `cg_rf_receiver_firewall.cpp`

- Connects to the receiver.
- Receives `(pk, Enc_pk(x))`.
- Samples mauling randomness.
- Computes `pk'` and converts the ciphertext to `pk'`.
- Rerandomizes and forwards it to S-RF.
- Receives the sender result from S-RF.
- Unmauls it back to the receiver key.
- Rerandomizes under the receiver key and sends it to the receiver.

### `cg_rf_sender_firewall.cpp`

- Connects to R-RF and receives Round 1 data.
- Rerandomizes the ciphertext for the sender.
- Also demonstrates a second sender-side mauling path with `fpk`.
- Listens for the sender.
- Sends the prepared public keys/ciphertexts to the sender.
- Receives encrypted results from the sender.
- Rerandomizes/unmauls as needed and forwards results back to R-RF.

### `cg_rf_sender.cpp`

- Parses sender inputs `a` and `b`.
- Connects to S-RF.
- Receives `pk'` and encrypted receiver input.
- Computes encrypted `a*x+b` using scalar multiplication, encryption of `b`,
  and homomorphic addition.
- Sends the encrypted result back to S-RF.

## OPA Demo Files

### `cg_rf_opa_sender.cpp`

- Parses degree `m`, polynomial `p_A`, and polynomial `r_A`.
- Evaluates both polynomials at `2m+1` public points.
- Connects to S-RF and sends the number of OLE points.
- For each point, receives encrypted `p_B(alpha_i)`.
- Computes encrypted `p_A(alpha_i) + r_A(alpha_i)*p_B(alpha_i)`.
- Sends each encrypted evaluation back through the firewall chain.

### `cg_rf_opa_receiver.cpp`

- Parses degree `m` and receiver polynomial `p_B`.
- Computes `2m+1` public evaluation points.
- Generates receiver keys.
- Listens for the firewall chain.
- Receives the expected number of points.
- For each point, encrypts `p_B(alpha_i)` and sends it.
- Receives and decrypts each interpolating-polynomial evaluation.
- Prints `(alpha_i, y_i)` pairs.

### `cg_rf_sender_firewall_opa.cpp`

- Connects to R-RF and listens for the OPA/PSI sender.
- Receives the point count from the sender and forwards it to R-RF.
- For each point:
  - receives `(pk', ciphertext)` from R-RF;
  - rerandomizes the ciphertext;
  - forwards it to the sender;
  - receives the sender's encrypted result;
  - rerandomizes and sends it back to R-RF.

### `cg_rf_receiver_firewall_opa.cpp`

- Connects to the receiver and listens for S-RF.
- Receives the OLE point count from S-RF and forwards it to the receiver.
- For each point:
  - receives `(pk, Enc(x_i))` from the receiver;
  - mauls the key and ciphertext;
  - rerandomizes and sends them to S-RF;
  - receives the encrypted result from S-RF;
  - unmauls and rerandomizes it;
  - sends it to the receiver.

## PSI Demo Files

### `cg_rf_psi_sender.cpp`

- Parses sender set `S_A`.
- Builds `p_A(X) = product(X - a_i)` from the sender set.
- Samples random masking polynomial `r_A`.
- Evaluates `p_A` and `r_A` at `2m+1` public points.
- Runs the OPA sender loop to compute encrypted masked evaluations.

### `cg_rf_psi_receiver.cpp`

- Parses receiver set `S_B`.
- Builds `p_B(X) = product(X - b_i)`.
- Generates receiver keys and listens for the firewall chain.
- Sends encrypted `p_B(alpha_i)` values.
- Receives and decrypts masked interpolating-polynomial evaluations.
- Uses Lagrange interpolation at each receiver set element.
- Prints elements whose interpolated value is zero modulo `q`, which indicates
  intersection membership.

## `CMakeLists.txt`

- Requires CMake 3.16 and declares a C++ project.
- Uses C++17.
- Adds optimization flags.
- Locates OpenSSL, GMP, and GMPXX.
- Creates an interface library for BICYCL/CG-AHE include paths and libraries.
- Defines helper function `cg_binary(name src)` to compile and link each binary.
- Registers the active RF-OLE and benchmark binaries.

## Launcher Scripts

### `run_rf_cg_ole_local.sh`

- Chooses input defaults for `x`, `a`, and `b`.
- Creates the log directory.
- Starts receiver, receiver firewall, sender firewall, and sender in order.
- Waits for all processes.
- Prints logs so the decrypted result can be inspected.

### `run_rf_cg_opa_local.sh`

- Chooses default OPA polynomials.
- Starts OPA receiver, receiver firewall, sender firewall, and OPA sender.
- Waits for all processes.
- Prints the receiver's decrypted OPA evaluation results.

### `run_rf_cg_psi_local.sh`

- Chooses default sender and receiver sets.
- Starts PSI receiver, receiver firewall, sender firewall, and PSI sender.
- Waits for all processes.
- Prints the receiver's computed intersection.
