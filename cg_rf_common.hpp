#pragma once
/*
 * Core CG/RF utility layer.
 *
 * Centralizes class-group parameter creation, randomness, timing-file writes,
 * polynomial arithmetic/evaluation, Lagrange interpolation, and key-malleable
 * ciphertext transforms shared by all protocols.
 */

#include <vector>
#include <chrono>
#include <iostream>
#include <utility>
#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <sstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include "cg_ahe/cg_ahe.hpp"
#include "cg_network.hpp"
#include "cg_thread_pool.hpp"

#if defined(CG_USE_NTL_POLY)
#include <NTL/ZZ.h>
#include <NTL/ZZ_p.h>
#include <NTL/ZZ_pX.h>
#endif

// ── CG-AHE parameters ────────────────────────────────────────────────────
// Defaults are production-oriented and can be overridden for benchmarking:
//   CG_Q_NBITS=128  selects the 128-bit plaintext field used in the reported benchmarks.
//   CG_K=1          is the CL_HSMqk parameter used by the current scheme.
inline size_t cg_env_size(const char* name, size_t fallback)
{
    const char* env = std::getenv(name);
    if (!env || !*env) return fallback;
    char* end = nullptr;
    unsigned long v = std::strtoul(env, &end, 10);
    return (end == env || v == 0) ? fallback : (size_t)v;
}

static const size_t        CG_Q_NBITS   = cg_env_size("CG_Q_NBITS", 256);
static const size_t        CG_K         = cg_env_size("CG_K", 1);
static const BICYCL::SecLevel  CG_SECLEVEL  = BICYCL::SecLevel::_128;
static constexpr const char* CG_NTT128_PRIME_DEC =
    "170141183460469232709364739622490341377";

// ── Port layout ───────────────────────────────────────────────────────────
//  Receiver :9003  ←RF_R:9002←  RF_S :9001  ←Sender(client)
static constexpr const char* PORT_REC  = "9003";
static constexpr const char* PORT_RFR  = "9002";
static constexpr const char* PORT_RFS  = "9001";
static constexpr const char* PORT_VERIFY = "9004";
static constexpr const char* LOCALHOST = "127.0.0.1";

inline const char* env_or_default(const char* name, const char* fallback)
{
    const char* value = std::getenv(name);
    return (value && *value) ? value : fallback;
}

// Resolve the three RF pipeline ports, allowing scripts to override them when
// multiple experiments run on the same host.
inline const char* rf_port_rec() { return env_or_default("CG_PORT_REC", PORT_REC); }
inline const char* rf_port_rfr() { return env_or_default("CG_PORT_RFR", PORT_RFR); }
inline const char* rf_port_rfs() { return env_or_default("CG_PORT_RFS", PORT_RFS); }

// ── Randomness ────────────────────────────────────────────────────────────
// Production protocol randomness is seeded from the OS CSPRNG.  The seeded
// generator below is deliberately separated and is used only for reproducible
// benchmark inputs/public parameters, never for encryption or masking secrets.

inline BICYCL::Mpz mpz_from_bytes(const std::vector<unsigned char>& bytes)
{
    BICYCL::Mpz x;
    x = bytes;
    return x;
}

inline std::vector<unsigned char> os_random_bytes(size_t n)
{
    std::vector<unsigned char> bytes(n);
    std::ifstream in("/dev/urandom", std::ios::binary);
    if (!in)
        throw std::runtime_error("cannot open /dev/urandom for CSPRNG seed");
    in.read(reinterpret_cast<char*>(bytes.data()), (std::streamsize)bytes.size());
    if ((size_t)in.gcount() != bytes.size())
        throw std::runtime_error("short read from /dev/urandom");
    return bytes;
}

inline BICYCL::RandGen make_secure_randgen()
{
    BICYCL::RandGen rng;
    rng.set_seed(mpz_from_bytes(os_random_bytes(32)));
    return rng;
}

inline BICYCL::RandGen make_seeded_benchmark_randgen(
    uint64_t seed,
    uint64_t domain)
{
    // Deterministic mode is only for reproducible benchmarks. Production
    // protocol randomness uses make_secure_randgen() above.
    std::vector<unsigned char> bytes(32, 0);
    uint64_t x = seed ^ (0x9e3779b97f4a7c15ULL + (domain << 1));
    for (size_t i = 0; i < bytes.size(); ++i) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        uint64_t z = x * 0x2545f4914f6cdd1dULL;
        bytes[i] = (unsigned char)(z >> ((i % 8) * 8));
    }
    BICYCL::RandGen rng;
    rng.set_seed(mpz_from_bytes(bytes));
    return rng;
}

inline uint64_t cg_public_seed()
{
    const char* env = std::getenv("CG_PUBLIC_SEED");
    if (!env || !*env) return 0x43475f5055424c49ULL;
    char* end = nullptr;
    unsigned long long v = std::strtoull(env, &end, 10);
    return (end == env) ? 0x43475f5055424c49ULL : (uint64_t)v;
}

inline BICYCL::RandGen make_public_param_randgen()
{
    // All parties must construct identical class-group public parameters.
    // Secret/runtime randomness is passed separately to make_cg_scheme().
    return make_seeded_benchmark_randgen(cg_public_seed(),
                                         0x5055425f5041524dULL);
}

inline CG_AHE::CG_Scheme make_cg_scheme(BICYCL::RandGen& runtime_randgen)
{
    // Public parameters must match across parties; keygen/encryption randomness
    // comes from runtime_randgen, which should be a secure per-process RNG.
    BICYCL::RandGen public_randgen = make_public_param_randgen();
    const char* fixed_q = std::getenv("CG_FIXED_Q");
    if (fixed_q && *fixed_q) {
        BICYCL::Mpz q(fixed_q);
        return CG_AHE::CG_Scheme(q, CG_K, CG_SECLEVEL,
                                 public_randgen, runtime_randgen);
    }
    return CG_AHE::CG_Scheme(CG_Q_NBITS, CG_K, CG_SECLEVEL,
                             public_randgen, runtime_randgen);
}

inline void enable_psi_ntt_plaintext_prime()
{
    // 128-bit NTT prime q = 9223372036854775861 * 2^64 + 1.  It is prime,
    // satisfies q = 1 mod 2^64, and meets BICYCL's 128-bit plaintext-q check.
    if (!std::getenv("CG_FIXED_Q"))
        setenv("CG_FIXED_Q", CG_NTT128_PRIME_DEC, 0);
}

inline BICYCL::RandGen& thread_secure_randgen()
{
    // Worker threads use their own OS-seeded RNG so parallel encryption/masking
    // never races on a shared randomness object.
    thread_local BICYCL::RandGen rng = make_secure_randgen();
    return rng;
}

inline BICYCL::Mpz cg_pow2_mpz(size_t bits)
{
    BICYCL::Mpz out(1UL);
    for (size_t i = 0; i < bits; ++i)
        BICYCL::Mpz::mul(out, out, BICYCL::Mpz(2UL));
    return out;
}

inline BICYCL::Mpz benchmark_input_bound(const BICYCL::Mpz& q)
{
    // Benchmark inputs may be deliberately smaller than the plaintext field
    // (for example 64-bit test values inside a 128-bit field).  Protocol
    // masks and cryptographic randomness still use their full domains.
    size_t bits = cg_env_size("CG_BENCH_INPUT_BITS", 128);
    BICYCL::Mpz bound = cg_pow2_mpz(bits);
    if (bound > q)
        return q;
    return bound;
}

inline std::vector<BICYCL::Mpz> read_mpz_list_file(
    const std::string& path,
    const BICYCL::Mpz& q,
    const char* role)
{
    // Strict field-element reader: used for set inputs that should already be
    // valid elements of Z_q.  It rejects negatives and values >= q.
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error(std::string(role) + ": cannot open input file " + path);

    std::vector<BICYCL::Mpz> values;
    std::string token;
    while (in >> token) {
        BICYCL::Mpz x(token.c_str());
        if (x.sgn() < 0 || x >= q) {
            std::ostringstream oss;
            oss << role << ": input value outside plaintext field Z_q in "
                << path << ": " << token;
            throw std::runtime_error(oss.str());
        }
        values.push_back(x);
    }
    if (values.empty())
        throw std::runtime_error(std::string(role) + ": input file is empty: " + path);
    return values;
}

inline std::vector<BICYCL::Mpz> read_mpz_list_file_mod_q(
    const std::string& path,
    const BICYCL::Mpz& q,
    const char* role)
{
    // Reduction reader: used for benchmark inputs generated as large integers.
    // Values are reduced modulo q before the protocol clock starts.
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error(std::string(role) + ": cannot open input file " + path);

    std::vector<BICYCL::Mpz> values;
    std::string token;
    while (in >> token) {
        BICYCL::Mpz x(token.c_str());
        if (x.sgn() < 0) {
            std::ostringstream oss;
            oss << role << ": negative input value in " << path << ": " << token;
            throw std::runtime_error(oss.str());
        }
        BICYCL::Mpz::mod(x, x, q);
        values.push_back(x);
    }
    if (values.empty())
        throw std::runtime_error(std::string(role) + ": input file is empty: " + path);
    return values;
}

inline std::vector<BICYCL::Mpz> read_mpz_coeff_file(
    const std::string& path,
    const BICYCL::Mpz& q,
    const char* role)
{
    // Polynomial coefficient reader.  Coefficients may be larger than q in input
    // files, so they are reduced into Z_q before use.
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error(std::string(role) + ": cannot open coefficient file " + path);

    std::vector<BICYCL::Mpz> values;
    std::string token;
    while (in >> token) {
        BICYCL::Mpz x(token.c_str());
        if (x.sgn() < 0) {
            std::ostringstream oss;
            oss << role << ": negative coefficient in "
                << path << ": " << token;
            throw std::runtime_error(oss.str());
        }
        BICYCL::Mpz::mod(x, x, q);
        values.push_back(x);
    }
    if (values.empty())
        throw std::runtime_error(std::string(role) + ": coefficient file is empty: " + path);
    return values;
}

inline void write_mpz_list_file(
    const std::string& path,
    const std::vector<BICYCL::Mpz>& values)
{
    // Plain one-value-per-line writer used only for offline checking/output.
    // Call sites place this after protocol timing has stopped.
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot open output file " + path);
    for (const auto& value : values)
        out << value << '\n';
}

inline void write_protocol_timing_file(
    const std::string& path,
    const std::string& component,
    double protocol_time_ms)
{
    // Artifact timing contract:
    //   protocol_time_excluding_input_and_output_files starts after all input
    //   files/arguments have been parsed and stops when the receiver/result
    //   value exists in memory.  Correctness dumps and CSV/log writes happen
    //   after this value is captured.
    if (path.empty())
        return;
    std::ofstream out(path);
    if (!out)
        throw std::runtime_error("cannot open protocol timing file " + path);
    out << "component,metric,value_ms\n";
    out << component << ",protocol_time_excluding_input_and_output_files,"
        << protocol_time_ms << "\n";
}

inline void write_protocol_timing_file_from_env(
    const std::string& component,
    double protocol_time_ms)
{
    // Convenience wrapper used by scripts: if CG_PROTOCOL_TIMING_FILE is set,
    // the endpoint writes its party-local protocol time there.
    const char* path = std::getenv("CG_PROTOCOL_TIMING_FILE");
    if (path && *path)
        write_protocol_timing_file(path, component, protocol_time_ms);
}

inline bool cg_profile_ops_enabled()
{
    const char* env = std::getenv("CG_PROFILE_OPS");
    return env && *env && std::string(env) != "0";
}

inline std::vector<BICYCL::Mpz> benchmark_input_vector(
    size_t count,
    uint64_t domain,
    const BICYCL::Mpz& q)
{
    // Deterministic fallback input generator for local micro-tests.  Two-machine
    // benchmark runs normally pass explicit input files instead.
    BICYCL::RandGen rng = make_seeded_benchmark_randgen(cg_public_seed(), domain);
    BICYCL::Mpz bound = benchmark_input_bound(q);
    std::vector<BICYCL::Mpz> out(count);
    for (auto& x : out)
        x = rng.random_mpz(bound);
    return out;
}

// ── Wall-clock timer ──────────────────────────────────────────────────────
using Clock = std::chrono::steady_clock;
inline double ms_since(std::chrono::time_point<Clock> t0) {
    return std::chrono::duration<double,std::milli>(Clock::now()-t0).count();
}

// ── Maul helpers (need cs for compact_variant check) ─────────────────────
// These helpers implement the key-changing operations used by RF-OLE.  They
// preserve the encrypted plaintext while moving a ciphertext between related
// public keys pk and pk' = pk * h^r.

// Forward-maul ciphertext: ct_pk' = (c1, c2 · c1^{r_maul})
// Converts Enc_pk(m) → Enc_pk'(m) where pk'=pk·h^{r_maul}
inline CG_AHE::CipherText maul_ct_forward(
    const CG_AHE::CipherText& ct,
    const BICYCL::Mpz&        r_maul,
    const BICYCL::CL_HSMqk&  cs)
{
    BICYCL::QFI R_pow_r;
    cs.Cl_G().nupow(R_pow_r, ct.c1(), r_maul);
    if (cs.compact_variant())
        cs.from_Cl_DeltaK_to_Cl_Delta(R_pow_r);
    BICYCL::QFI E_prime;
    cs.Cl_Delta().nucomp(E_prime, ct.c2(), R_pow_r);
    return CG_AHE::CipherText(ct.c1(), E_prime);
}

// Unmaul ciphertext: ct_pk = (c1, c2 · c1^{-r_maul})
// Converts Enc_pk'(m) → Enc_pk(m)
inline CG_AHE::CipherText maul_ct_inverse(
    const CG_AHE::CipherText& ct,
    const BICYCL::Mpz&        r_maul,
    const BICYCL::CL_HSMqk&  cs)
{
    BICYCL::QFI R_pow_r;
    cs.Cl_G().nupow(R_pow_r, ct.c1(), r_maul);
    if (cs.compact_variant())
        cs.from_Cl_DeltaK_to_Cl_Delta(R_pow_r);
    BICYCL::QFI E_unmaul;
    cs.Cl_Delta().nucompinv(E_unmaul, ct.c2(), R_pow_r);
    return CG_AHE::CipherText(ct.c1(), E_unmaul);
}

// Maul public key: pk' = pk · h^{r_maul}
inline CG_AHE::PublicKey maul_pk(
    const CG_AHE::PublicKey& pk,
    const BICYCL::Mpz&       r_maul,
    const BICYCL::CL_HSMqk& cs)
{
    BICYCL::QFI h_pow_r;
    cs.power_of_h(h_pow_r, r_maul);
    BICYCL::QFI new_elt;
    cs.Cl_G().nucomp(new_elt, pk.elt(), h_pow_r);
    return CG_AHE::PublicKey(cs, new_elt);
}

// ── FUSED: MaulCT-forward + ReRand (3 group ops instead of 1 nupow + 4 nucomp)
// This is the critical online-phase hot path in both firewalls.
// Instead of: maul_ct_forward → nucomp(c1, R) + nucomp(c2_mauled, E)
//   Step 1: c1^r_maul            [1 nupow in Cl_G]
//   Step 2: c2' = c2 · c1^r     [1 nucomp in Cl_Delta]
//   Step 3: out.c1 = c1 · pre_R  [1 nucomp in Cl_G]  (c1 unchanged by mauling)
//   Step 4: out.c2 = c2' · pre_E [1 nucomp in Cl_Delta]
// Fused: c1 is shared → out.c1 = nucomp(c1, pre_R) and c2' uses R_pow_r
// Net: 1 nupow + 3 nucomp = saves 1 nucomp vs sequential.
struct EncZero { BICYCL::QFI R, E; };

inline CG_AHE::CipherText maul_fwd_rerand(
    const CG_AHE::CipherText& ct,
    const BICYCL::Mpz&        r_maul,
    const EncZero&            pre,       // precomputed (h^s, pk^s) pair
    const BICYCL::CL_HSMqk&  cs)
{
    // Firewall hot path: align ciphertext from pk to mauled pk' and refresh its
    // randomness using a precomputed encryption of zero.
    // c1^r (for ciphertext key-switching in c2)
    BICYCL::QFI R_pow_r;
    cs.Cl_G().nupow(R_pow_r, ct.c1(), r_maul);
    if (cs.compact_variant()) cs.from_Cl_DeltaK_to_Cl_Delta(R_pow_r);

    // out.c1 = c1 · pre.R  (c1 stays in Cl_G — no mauling on c1)
    BICYCL::QFI out_c1;
    cs.Cl_G().nucomp(out_c1, ct.c1(), pre.R);

    // out.c2 = (c2 · c1^r) · pre.E   [merged maul + rerand in one pass]
    BICYCL::QFI c2_mauled;
    cs.Cl_Delta().nucomp(c2_mauled, ct.c2(), R_pow_r);
    BICYCL::QFI out_c2;
    cs.Cl_Delta().nucomp(out_c2, c2_mauled, pre.E);

    return CG_AHE::CipherText(out_c1, out_c2);
}

// FUSED: MaulCT-inverse + ReRand
inline CG_AHE::CipherText maul_inv_rerand(
    const CG_AHE::CipherText& ct,
    const BICYCL::Mpz&        r_maul,
    const EncZero&            pre,
    const BICYCL::CL_HSMqk&  cs)
{
    // Inverse firewall hot path: align ciphertext from pk' back to pk and
    // rerandomize in the same pass.
    BICYCL::QFI R_pow_r;
    cs.Cl_G().nupow(R_pow_r, ct.c1(), r_maul);
    if (cs.compact_variant()) cs.from_Cl_DeltaK_to_Cl_Delta(R_pow_r);

    BICYCL::QFI out_c1;
    cs.Cl_G().nucomp(out_c1, ct.c1(), pre.R);

    // c2 / c1^r  then  · pre.E
    BICYCL::QFI c2_unmauled;
    cs.Cl_Delta().nucompinv(c2_unmauled, ct.c2(), R_pow_r);
    BICYCL::QFI out_c2;
    cs.Cl_Delta().nucomp(out_c2, c2_unmauled, pre.E);

    return CG_AHE::CipherText(out_c1, out_c2);
}

struct MaulRerandProfile {
    double nupow_ms = 0.0;       // c1^r, including compact conversion if enabled
    double c1_rerand_ms = 0.0;   // out.c1 = c1 * pre.R
    double c2_maul_ms = 0.0;     // c2 * c1^r or c2 / c1^r
    double c2_rerand_ms = 0.0;   // c2_mauled * pre.E
};

inline CG_AHE::CipherText maul_fwd_rerand_profiled(
    const CG_AHE::CipherText& ct,
    const BICYCL::Mpz&        r_maul,
    const EncZero&            pre,
    const BICYCL::CL_HSMqk&  cs,
    MaulRerandProfile&        prof)
{
    // Profiling variant of maul_fwd_rerand; same output, but records per-group
    // operation timings for firewall microbenchmarks.
    auto t = Clock::now();
    BICYCL::QFI R_pow_r;
    cs.Cl_G().nupow(R_pow_r, ct.c1(), r_maul);
    if (cs.compact_variant()) cs.from_Cl_DeltaK_to_Cl_Delta(R_pow_r);
    prof.nupow_ms = ms_since(t);

    t = Clock::now();
    BICYCL::QFI out_c1;
    cs.Cl_G().nucomp(out_c1, ct.c1(), pre.R);
    prof.c1_rerand_ms = ms_since(t);

    t = Clock::now();
    BICYCL::QFI c2_mauled;
    cs.Cl_Delta().nucomp(c2_mauled, ct.c2(), R_pow_r);
    prof.c2_maul_ms = ms_since(t);

    t = Clock::now();
    BICYCL::QFI out_c2;
    cs.Cl_Delta().nucomp(out_c2, c2_mauled, pre.E);
    prof.c2_rerand_ms = ms_since(t);

    return CG_AHE::CipherText(out_c1, out_c2);
}

inline CG_AHE::CipherText maul_inv_rerand_profiled(
    const CG_AHE::CipherText& ct,
    const BICYCL::Mpz&        r_maul,
    const EncZero&            pre,
    const BICYCL::CL_HSMqk&  cs,
    MaulRerandProfile&        prof)
{
    // Profiling variant of maul_inv_rerand; used to locate the cost of inverse
    // maul and rerandomization separately.
    auto t = Clock::now();
    BICYCL::QFI R_pow_r;
    cs.Cl_G().nupow(R_pow_r, ct.c1(), r_maul);
    if (cs.compact_variant()) cs.from_Cl_DeltaK_to_Cl_Delta(R_pow_r);
    prof.nupow_ms = ms_since(t);

    t = Clock::now();
    BICYCL::QFI out_c1;
    cs.Cl_G().nucomp(out_c1, ct.c1(), pre.R);
    prof.c1_rerand_ms = ms_since(t);

    t = Clock::now();
    BICYCL::QFI c2_unmauled;
    cs.Cl_Delta().nucompinv(c2_unmauled, ct.c2(), R_pow_r);
    prof.c2_maul_ms = ms_since(t);

    t = Clock::now();
    BICYCL::QFI out_c2;
    cs.Cl_Delta().nucomp(out_c2, c2_unmauled, pre.E);
    prof.c2_rerand_ms = ms_since(t);

    return CG_AHE::CipherText(out_c1, out_c2);
}

// ── Polynomial helpers (coefficients in Z_q, index 0 = constant term) ────

inline void poly_trim(std::vector<BICYCL::Mpz>& p)
{
    // Keep a canonical coefficient vector representation: no trailing zeros
    // except for the zero polynomial, which remains length one.
    BICYCL::Mpz zero(0UL);
    while (p.size() > 1 && p.back() == zero)
        p.pop_back();
}

inline void poly_mod_q_inplace(std::vector<BICYCL::Mpz>& p, const BICYCL::Mpz& q)
{
    // Normalize every coefficient modulo q and then trim the polynomial.
    for (auto& c : p)
        BICYCL::Mpz::mod(c, c, q);
    poly_trim(p);
}

inline bool is_power_of_two_size(size_t n)
{
    return n != 0 && (n & (n - 1)) == 0;
}

inline size_t next_power_of_two_size(size_t n)
{
    if (n <= 1) return 1;
    size_t p = 1;
    while (p < n) {
        if (p > (std::numeric_limits<size_t>::max() >> 1))
            throw std::runtime_error("next_power_of_two_size: size overflow");
        p <<= 1;
    }
    return p;
}

inline bool ntt_runtime_enabled()
{
    const char* env = std::getenv("CG_USE_NTT_POLY");
    return !(env && *env && std::string(env) == "0");
}

inline bool is_ntt128_prime(const BICYCL::Mpz& q)
{
    return q == BICYCL::Mpz(CG_NTT128_PRIME_DEC);
}

inline bool ntt_domain_supported(const BICYCL::Mpz& q, size_t n)
{
    return ntt_runtime_enabled() && is_ntt128_prime(q) && is_power_of_two_size(n);
}

inline BICYCL::Mpz ntt_primitive_root(size_t n, const BICYCL::Mpz& q)
{
    if (!ntt_domain_supported(q, n))
        throw std::runtime_error("ntt_primitive_root: unsupported q or domain size");
    if (n == 1)
        return BICYCL::Mpz(1UL);

    BICYCL::Mpz exp;
    BICYCL::Mpz::sub(exp, q, BICYCL::Mpz(1UL));
    BICYCL::Mpz::divexact(exp, exp, (unsigned long)n);

    BICYCL::Mpz root;
    BICYCL::Mpz::pow_mod(root, BICYCL::Mpz(3UL), exp, q);

    BICYCL::Mpz check;
    BICYCL::Mpz::pow_mod(check, root, BICYCL::Mpz((unsigned long)n), q);
    if (check != BICYCL::Mpz(1UL))
        throw std::runtime_error("ntt_primitive_root: root order check failed");
    BICYCL::Mpz::pow_mod(check, root, BICYCL::Mpz((unsigned long)(n / 2)), q);
    if (check == BICYCL::Mpz(1UL))
        throw std::runtime_error("ntt_primitive_root: non-primitive root");
    return root;
}

inline std::vector<BICYCL::Mpz> ntt_domain_points(size_t n, const BICYCL::Mpz& q)
{
    std::vector<BICYCL::Mpz> points(n, BICYCL::Mpz(1UL));
    if (n == 0) return points;
    BICYCL::Mpz omega = ntt_primitive_root(n, q);
    for (size_t i = 1; i < n; ++i) {
        BICYCL::Mpz::mul(points[i], points[i - 1], omega);
        BICYCL::Mpz::mod(points[i], points[i], q);
    }
    return points;
}

inline void ntt_inplace(std::vector<BICYCL::Mpz>& a, const BICYCL::Mpz& q, bool inverse)
{
    const size_t n = a.size();
    if (!ntt_domain_supported(q, n))
        throw std::runtime_error("ntt_inplace: unsupported q or non-power-of-two size");

    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        BICYCL::Mpz wlen = ntt_primitive_root(len, q);
        if (inverse)
            BICYCL::Mpz::mod_inverse(wlen, wlen, q);

        for (size_t i = 0; i < n; i += len) {
            BICYCL::Mpz w(1UL);
            for (size_t j = 0; j < len / 2; ++j) {
                BICYCL::Mpz u = a[i + j];
                BICYCL::Mpz v;
                BICYCL::Mpz::mul(v, a[i + j + len / 2], w);
                BICYCL::Mpz::mod(v, v, q);

                BICYCL::Mpz::add(a[i + j], u, v);
                BICYCL::Mpz::mod(a[i + j], a[i + j], q);

                BICYCL::Mpz::sub(a[i + j + len / 2], u, v);
                BICYCL::Mpz::mod(a[i + j + len / 2], a[i + j + len / 2], q);

                BICYCL::Mpz::mul(w, w, wlen);
                BICYCL::Mpz::mod(w, w, q);
            }
        }
    }

    if (inverse) {
        BICYCL::Mpz n_inv;
        BICYCL::Mpz::mod_inverse(n_inv, BICYCL::Mpz((unsigned long)n), q);
        for (auto& x : a) {
            BICYCL::Mpz::mul(x, x, n_inv);
            BICYCL::Mpz::mod(x, x, q);
        }
    }
}

inline size_t ntt_poly_threshold()
{
    const char* env = std::getenv("CG_NTT_POLY_THRESHOLD");
    if (!env || !*env) return 512;
    char* end = nullptr;
    unsigned long v = std::strtoul(env, &end, 10);
    return (end == env) ? 512 : (size_t)v;
}

inline std::vector<BICYCL::Mpz> poly_slice(
    const std::vector<BICYCL::Mpz>& p,
    size_t begin,
    size_t end)
{
    // Safe half-open coefficient slice used by recursive multiplication.
    begin = std::min(begin, p.size());
    end = std::min(end, p.size());
    if (end <= begin) return {BICYCL::Mpz(0UL)};
    return std::vector<BICYCL::Mpz>(p.begin() + begin, p.begin() + end);
}

inline std::vector<BICYCL::Mpz> poly_add_mod(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    // Coefficient-wise addition in Z_q.
    size_t n = std::max(a.size(), b.size());
    std::vector<BICYCL::Mpz> out(n, BICYCL::Mpz(0UL));
    for (size_t i = 0; i < n; ++i) {
        if (i < a.size()) BICYCL::Mpz::add(out[i], out[i], a[i]);
        if (i < b.size()) BICYCL::Mpz::add(out[i], out[i], b[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    }
    poly_trim(out);
    return out;
}

inline std::vector<BICYCL::Mpz> poly_sub_mod(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    // Coefficient-wise subtraction in Z_q.
    size_t n = std::max(a.size(), b.size());
    std::vector<BICYCL::Mpz> out(n, BICYCL::Mpz(0UL));
    for (size_t i = 0; i < n; ++i) {
        if (i < a.size()) BICYCL::Mpz::add(out[i], out[i], a[i]);
        if (i < b.size()) BICYCL::Mpz::sub(out[i], out[i], b[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    }
    poly_trim(out);
    return out;
}

inline std::vector<BICYCL::Mpz> poly_mul_naive_mod(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    // Quadratic multiplication for small polynomials.
    if (a.empty() || b.empty()) return {BICYCL::Mpz(0UL)};

    std::vector<BICYCL::Mpz> out(a.size() + b.size() - 1, BICYCL::Mpz(0UL));
    for (size_t i = 0; i < a.size(); ++i) {
        for (size_t j = 0; j < b.size(); ++j) {
            BICYCL::Mpz term;
            BICYCL::Mpz::mul(term, a[i], b[j]);
            BICYCL::Mpz::add(out[i + j], out[i + j], term);
        }
    }
    for (auto& c : out)
        BICYCL::Mpz::mod(c, c, q);
    poly_trim(out);
    return out;
}

#if defined(CG_USE_NTL_POLY)
inline NTL::ZZ ntl_zz_from_mpz(const BICYCL::Mpz& x)
{
    std::ostringstream oss;
    oss << x;
    return NTL::conv<NTL::ZZ>(oss.str().c_str());
}

inline BICYCL::Mpz mpz_from_ntl_zz(const NTL::ZZ& x)
{
    std::ostringstream oss;
    oss << x;
    return BICYCL::Mpz(oss.str());
}

inline NTL::vec_ZZ_p ntl_vec_from_mpz_vec(
    const std::vector<BICYCL::Mpz>& values)
{
    NTL::vec_ZZ_p out;
    out.SetLength((long)values.size());
    for (long i = 0; i < out.length(); ++i)
        out[i] = NTL::conv<NTL::ZZ_p>(ntl_zz_from_mpz(values[(size_t)i]));
    return out;
}

inline NTL::ZZ_pX ntl_poly_from_coeffs(
    const std::vector<BICYCL::Mpz>& coeffs)
{
    NTL::ZZ_pX p;
    for (long i = 0; i < (long)coeffs.size(); ++i)
        NTL::SetCoeff(p, i, NTL::conv<NTL::ZZ_p>(ntl_zz_from_mpz(coeffs[(size_t)i])));
    return p;
}

inline std::vector<BICYCL::Mpz> mpz_vec_from_ntl_poly(const NTL::ZZ_pX& p)
{
    const long d = NTL::deg(p);
    if (d < 0) return {BICYCL::Mpz(0UL)};
    std::vector<BICYCL::Mpz> out((size_t)d + 1, BICYCL::Mpz(0UL));
    for (long i = 0; i <= d; ++i)
        out[(size_t)i] = mpz_from_ntl_zz(NTL::rep(NTL::coeff(p, i)));
    poly_trim(out);
    return out;
}

inline size_t ntl_poly_threshold()
{
    // Use NTL only where its asymptotically faster algorithms pay for the
    // BICYCL<->NTL conversion cost.  Set CG_NTL_POLY_THRESHOLD=0 to force it.
    const char* env = std::getenv("CG_NTL_POLY_THRESHOLD");
    if (!env || !*env) return 512;
    char* end = nullptr;
    unsigned long v = std::strtoul(env, &end, 10);
    return (end == env) ? 512 : (size_t)v;
}

inline bool ntl_poly_enabled()
{
    // Runtime kill switch for comparisons: CG_USE_NTL_POLY_RUNTIME=0.
    const char* env = std::getenv("CG_USE_NTL_POLY_RUNTIME");
    return !(env && *env && std::string(env) == "0");
}

inline std::vector<BICYCL::Mpz> poly_mul_ntl_mod(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    NTL::ZZ_pPush push(ntl_zz_from_mpz(q));
    NTL::ZZ_pX pa = ntl_poly_from_coeffs(a);
    NTL::ZZ_pX pb = ntl_poly_from_coeffs(b);
    return mpz_vec_from_ntl_poly(pa * pb);
}
#endif

inline std::vector<BICYCL::Mpz> poly_mul_ntt_mod(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    if (a.empty() || b.empty()) return {BICYCL::Mpz(0UL)};
    const size_t need = a.size() + b.size() - 1;
    const size_t n = next_power_of_two_size(need);
    if (!ntt_domain_supported(q, n))
        throw std::runtime_error("poly_mul_ntt_mod: unsupported NTT domain");

    std::vector<BICYCL::Mpz> fa(n, BICYCL::Mpz(0UL));
    std::vector<BICYCL::Mpz> fb(n, BICYCL::Mpz(0UL));
    for (size_t i = 0; i < a.size(); ++i) {
        fa[i] = a[i];
        BICYCL::Mpz::mod(fa[i], fa[i], q);
    }
    for (size_t i = 0; i < b.size(); ++i) {
        fb[i] = b[i];
        BICYCL::Mpz::mod(fb[i], fb[i], q);
    }

    ntt_inplace(fa, q, false);
    ntt_inplace(fb, q, false);
    for (size_t i = 0; i < n; ++i) {
        BICYCL::Mpz::mul(fa[i], fa[i], fb[i]);
        BICYCL::Mpz::mod(fa[i], fa[i], q);
    }
    ntt_inplace(fa, q, true);
    fa.resize(need);
    poly_trim(fa);
    return fa;
}

inline std::vector<BICYCL::Mpz> poly_mul_mod(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    // Karatsuba-style recursive multiplication above the cutoff, naive below.
    static constexpr size_t KARATSUBA_CUTOFF = 32;
    if (a.empty() || b.empty()) return {BICYCL::Mpz(0UL)};
    const size_t ntt_n = next_power_of_two_size(a.size() + b.size() - 1);
    if (std::max(a.size(), b.size()) >= ntt_poly_threshold()
        && ntt_domain_supported(q, ntt_n))
        return poly_mul_ntt_mod(a, b, q);
#if defined(CG_USE_NTL_POLY)
    if (ntl_poly_enabled() && std::max(a.size(), b.size()) >= ntl_poly_threshold())
        return poly_mul_ntl_mod(a, b, q);
#endif
    if (a.size() < KARATSUBA_CUTOFF || b.size() < KARATSUBA_CUTOFF)
        return poly_mul_naive_mod(a, b, q);

    size_t n = std::max(a.size(), b.size());
    size_t half = (n + 1) / 2;

    std::vector<BICYCL::Mpz> a0 = poly_slice(a, 0, half);
    std::vector<BICYCL::Mpz> a1 = poly_slice(a, half, a.size());
    std::vector<BICYCL::Mpz> b0 = poly_slice(b, 0, half);
    std::vector<BICYCL::Mpz> b1 = poly_slice(b, half, b.size());

    std::vector<BICYCL::Mpz> z0 = poly_mul_mod(a0, b0, q);
    std::vector<BICYCL::Mpz> z2 = poly_mul_mod(a1, b1, q);
    std::vector<BICYCL::Mpz> a01 = poly_add_mod(a0, a1, q);
    std::vector<BICYCL::Mpz> b01 = poly_add_mod(b0, b1, q);
    std::vector<BICYCL::Mpz> z1 = poly_mul_mod(a01, b01, q);
    z1 = poly_sub_mod(z1, z0, q);
    z1 = poly_sub_mod(z1, z2, q);

    std::vector<BICYCL::Mpz> out(a.size() + b.size() - 1, BICYCL::Mpz(0UL));
    for (size_t i = 0; i < z0.size(); ++i) {
        BICYCL::Mpz::add(out[i], out[i], z0[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    }
    for (size_t i = 0; i < z1.size(); ++i) {
        if (i + half >= out.size()) break;
        BICYCL::Mpz::add(out[i + half], out[i + half], z1[i]);
        BICYCL::Mpz::mod(out[i + half], out[i + half], q);
    }
    for (size_t i = 0; i < z2.size(); ++i) {
        if (i + 2 * half >= out.size()) break;
        BICYCL::Mpz::add(out[i + 2 * half], out[i + 2 * half], z2[i]);
        BICYCL::Mpz::mod(out[i + 2 * half], out[i + 2 * half], q);
    }
    poly_trim(out);
    return out;
}

inline std::vector<BICYCL::Mpz> poly_linear_from_root(
    const BICYCL::Mpz& root,
    const BICYCL::Mpz& q)
{
    // Return X - root over Z_q, represented as coefficients [-root, 1].
    BICYCL::Mpz x_mod;
    BICYCL::Mpz::mod(x_mod, root, q);

    BICYCL::Mpz c0(0UL);
    if (x_mod != BICYCL::Mpz(0UL))
        BICYCL::Mpz::sub(c0, q, x_mod);

    return {c0, BICYCL::Mpz(1UL)};
}

struct PolyProductTree {
    // levels[0] stores (X - x_i); the last level stores the product polynomial.
    std::vector<std::vector<std::vector<BICYCL::Mpz>>> levels;
};

inline PolyProductTree poly_build_product_tree(
    const std::vector<BICYCL::Mpz>& points,
    const BICYCL::Mpz& q)
{
    // Build a subproduct tree for multipoint evaluation/interpolation.
    PolyProductTree tree;
    if (points.empty()) return tree;

    tree.levels.emplace_back();
    tree.levels[0].reserve(points.size());
    for (const auto& x : points)
        tree.levels[0].push_back(poly_linear_from_root(x, q));

    while (tree.levels.back().size() > 1) {
        const auto& prev = tree.levels.back();
        std::vector<std::vector<BICYCL::Mpz>> next((prev.size() + 1) / 2);
        global_pool().parallel_for(0, next.size(), [&](size_t i) {
            size_t li = 2 * i;
            size_t ri = li + 1;
            if (ri < prev.size())
                next[i] = poly_mul_mod(prev[li], prev[ri], q);
            else
                next[i] = prev[li];
        });
        tree.levels.push_back(std::move(next));
    }
    return tree;
}

inline std::string public_poly_cache_key(size_t n, const BICYCL::Mpz& q)
{
    std::ostringstream oss;
    oss << n << ':' << q;
    return oss.str();
}

inline const std::vector<BICYCL::Mpz>& public_eval_points_cached(
    size_t n,
    const BICYCL::Mpz& q)
{
    // Public OPA/PSI points use an NTT roots-of-unity domain when the fixed
    // 128-bit NTT prime and a power-of-two point count are active.  Otherwise
    // they fall back to alpha_i=i+1 for legacy OPA sizes.
    static std::mutex cache_mu;
    static std::unordered_map<std::string, std::vector<BICYCL::Mpz>> cache;

    const std::string key = public_poly_cache_key(n, q);
    std::lock_guard<std::mutex> lock(cache_mu);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;

    std::vector<BICYCL::Mpz> alpha;
    if (ntt_domain_supported(q, n)) {
        alpha = ntt_domain_points(n, q);
    } else {
        alpha.resize(n);
        for (size_t i = 0; i < n; ++i)
            alpha[i] = BICYCL::Mpz((unsigned long)(i + 1));
    }
    auto inserted = cache.emplace(key, std::move(alpha));
    return inserted.first->second;
}

inline bool is_public_eval_points(
    const std::vector<BICYCL::Mpz>& points,
    const BICYCL::Mpz& q)
{
    const auto& public_points = public_eval_points_cached(points.size(), q);
    for (size_t i = 0; i < points.size(); ++i) {
        if (points[i] != public_points[i])
            return false;
    }
    return true;
}

inline const PolyProductTree& public_product_tree_cached(
    size_t n,
    const BICYCL::Mpz& q)
{
    // Cache the subproduct tree for public points alpha_i=i+1.  The tree
    // depends on both n and q because polynomial coefficients are reduced mod q.
    static std::mutex cache_mu;
    static std::unordered_map<std::string, PolyProductTree> cache;

    const std::string key = public_poly_cache_key(n, q);
    std::lock_guard<std::mutex> lock(cache_mu);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;

    PolyProductTree tree = poly_build_product_tree(public_eval_points_cached(n, q), q);
    auto inserted = cache.emplace(key, std::move(tree));
    return inserted.first->second;
}

inline std::vector<BICYCL::Mpz> poly_from_roots(
    const std::vector<BICYCL::Mpz>& roots,
    const BICYCL::Mpz& q)
{
    // Construct the vanishing polynomial prod_i (X - roots[i]).
    if (roots.empty()) return {BICYCL::Mpz(1UL)};
#if defined(CG_USE_NTL_POLY)
    if (ntl_poly_enabled() && roots.size() >= ntl_poly_threshold()) {
        NTL::ZZ_pPush push(ntl_zz_from_mpz(q));
        NTL::ZZ_pX p;
        NTL::BuildFromRoots(p, ntl_vec_from_mpz_vec(roots));
        return mpz_vec_from_ntl_poly(p);
    }
#endif
    PolyProductTree tree = poly_build_product_tree(roots, q);
    return tree.levels.back().front();
}

// Horner evaluation of poly at x mod q
inline BICYCL::Mpz poly_eval(
    const std::vector<BICYCL::Mpz>& coeffs,
    const BICYCL::Mpz& x,
    const BICYCL::Mpz& q)
{
    if (coeffs.empty()) return BICYCL::Mpz(0UL);
    // Horner form evaluates from the highest coefficient downward:
    // (((c_d*x + c_{d-1})*x + ...) + c_0) mod q.
    BICYCL::Mpz res = coeffs.back();
    for (int i = (int)coeffs.size()-2; i >= 0; --i) {
        BICYCL::Mpz::mul(res, res, x);
        BICYCL::Mpz::add(res, res, coeffs[i]);
        BICYCL::Mpz::mod(res, res, q);
    }
    return res;
}

inline std::vector<BICYCL::Mpz> poly_mod_monic(
    const std::vector<BICYCL::Mpz>& dividend,
    const std::vector<BICYCL::Mpz>& divisor,
    const BICYCL::Mpz& q)
{
    // Compute dividend mod divisor for a monic divisor.  Product-tree
    // evaluation uses this to descend remainders to leaves.
    if (divisor.empty()) throw std::runtime_error("poly_mod_monic: empty divisor");
    if (dividend.empty()) return {BICYCL::Mpz(0UL)};

    std::vector<BICYCL::Mpz> rem = dividend;
    poly_mod_q_inplace(rem, q);

    if (rem.size() < divisor.size())
        return rem;

    const size_t d = divisor.size() - 1;
    if (d == 0)
        return {BICYCL::Mpz(0UL)};

    for (std::ptrdiff_t k = (std::ptrdiff_t)rem.size() - 1; k >= (std::ptrdiff_t)d; --k) {
        BICYCL::Mpz coeff = rem[(size_t)k];
        if (coeff != BICYCL::Mpz(0UL)) {
            for (size_t j = 0; j < d; ++j) {
                BICYCL::Mpz term;
                BICYCL::Mpz::mul(term, coeff, divisor[j]);
                const size_t dst = (size_t)k - d + j;
                BICYCL::Mpz::sub(rem[dst], rem[dst], term);
                BICYCL::Mpz::mod(rem[dst], rem[dst], q);
            }
        }
    }

    rem.resize(d);
    if (rem.empty()) rem.push_back(BICYCL::Mpz(0UL));
    poly_trim(rem);
    return rem;
}

inline std::vector<BICYCL::Mpz> poly_eval_batch_with_product_tree(
    const std::vector<BICYCL::Mpz>& coeffs,
    const PolyProductTree& tree,
    size_t n_points,
    const BICYCL::Mpz& q)
{
    // Multipoint evaluation by subproduct-tree remainder descent using a
    // caller-supplied tree.  Public OPA points pass a cached tree here.
    std::vector<BICYCL::Mpz> result(n_points, BICYCL::Mpz(0UL));
    if (n_points == 0) return result;
    if (coeffs.empty()) return result;
    if (tree.levels.empty() || tree.levels[0].size() != n_points)
        throw std::runtime_error("poly_eval_batch_with_product_tree: bad tree");

    std::vector<std::vector<BICYCL::Mpz>> rems;
    rems.push_back(poly_mod_monic(coeffs, tree.levels.back().front(), q));

    for (size_t level = tree.levels.size() - 1; level > 0; --level) {
        std::vector<std::vector<BICYCL::Mpz>> child_rems(tree.levels[level - 1].size());
        global_pool().parallel_for(0, rems.size(), [&](size_t i) {
            const size_t left = 2 * i;
            child_rems[left] = poly_mod_monic(rems[i], tree.levels[level - 1][left], q);
            if (left + 1 < child_rems.size())
                child_rems[left + 1] = poly_mod_monic(rems[i], tree.levels[level - 1][left + 1], q);
        });
        rems = std::move(child_rems);
    }

    for (size_t i = 0; i < n_points; ++i) {
        if (!rems[i].empty()) {
            result[i] = rems[i][0];
            BICYCL::Mpz::mod(result[i], result[i], q);
        }
    }
    return result;
}

inline std::vector<BICYCL::Mpz> poly_eval_batch_product_tree(
    const std::vector<BICYCL::Mpz>& coeffs,
    const std::vector<BICYCL::Mpz>& points,
    const BICYCL::Mpz& q)
{
    // Multipoint evaluation by subproduct-tree remainder descent.  The standard
    // public OPA/PSI points alpha_i=i+1 reuse a cached tree keyed by (N, q).
    if (points.empty())
        return std::vector<BICYCL::Mpz>();
    if (is_public_eval_points(points, q))
        return poly_eval_batch_with_product_tree(
            coeffs, public_product_tree_cached(points.size(), q), points.size(), q);

    PolyProductTree tree = poly_build_product_tree(points, q);
    return poly_eval_batch_with_product_tree(coeffs, tree, points.size(), q);
}

#if defined(CG_USE_NTL_POLY)
inline std::vector<BICYCL::Mpz> poly_eval_batch_ntl(
    const std::vector<BICYCL::Mpz>& coeffs,
    const std::vector<BICYCL::Mpz>& points,
    const BICYCL::Mpz& q)
{
    std::vector<BICYCL::Mpz> result(points.size(), BICYCL::Mpz(0UL));
    if (points.empty() || coeffs.empty()) return result;

    NTL::ZZ_pPush push(ntl_zz_from_mpz(q));
    NTL::ZZ_pX p = ntl_poly_from_coeffs(coeffs);
    NTL::vec_ZZ_p xs = ntl_vec_from_mpz_vec(points);
    NTL::vec_ZZ_p ys;
    NTL::eval(ys, p, xs);

    for (long i = 0; i < ys.length(); ++i)
        result[(size_t)i] = mpz_from_ntl_zz(NTL::rep(ys[i]));
    return result;
}
#endif

inline std::vector<BICYCL::Mpz> poly_eval_batch_ntt(
    const std::vector<BICYCL::Mpz>& coeffs,
    size_t n_points,
    const BICYCL::Mpz& q)
{
    if (!ntt_domain_supported(q, n_points))
        throw std::runtime_error("poly_eval_batch_ntt: unsupported NTT domain");
    if (coeffs.size() > n_points)
        throw std::runtime_error("poly_eval_batch_ntt: polynomial degree exceeds NTT domain");
    std::vector<BICYCL::Mpz> values(n_points, BICYCL::Mpz(0UL));
    for (size_t i = 0; i < coeffs.size(); ++i) {
        values[i] = coeffs[i];
        BICYCL::Mpz::mod(values[i], values[i], q);
    }
    ntt_inplace(values, q, false);
    return values;
}

inline std::vector<BICYCL::Mpz> lagrange_interpolate_ntt(
    const std::vector<BICYCL::Mpz>& y,
    const BICYCL::Mpz& q)
{
    if (!ntt_domain_supported(q, y.size()))
        throw std::runtime_error("lagrange_interpolate_ntt: unsupported NTT domain");
    std::vector<BICYCL::Mpz> coeffs = y;
    for (auto& c : coeffs)
        BICYCL::Mpz::mod(c, c, q);
    ntt_inplace(coeffs, q, true);
    poly_trim(coeffs);
    return coeffs;
}

inline std::vector<BICYCL::Mpz> poly_eval_batch_horner(
    const std::vector<BICYCL::Mpz>& coeffs,
    const std::vector<BICYCL::Mpz>& points,
    const BICYCL::Mpz& q)
{
    // Parallel Horner evaluation at many points.  This is often faster for the
    // benchmark sizes despite worse asymptotic complexity.
    std::vector<BICYCL::Mpz> result(points.size(), BICYCL::Mpz(0UL));
    if (points.empty() || coeffs.empty()) return result;

    // For the PSI sizes we use most often, parallel Horner beats the
    // product-tree evaluator because BICYCL/GMP object overhead dominates
    // the asymptotic win of subproduct-tree remainder descent.
    global_pool().parallel_for(0, points.size(), [&](size_t i) {
        result[i] = poly_eval(coeffs, points[i], q);
    }, 8);
    return result;
}

inline size_t poly_eval_auto_horner_max_points()
{
    // Tuning knob for auto evaluation: below this many points, prefer Horner.
    // The generic product-tree path has high BICYCL/GMP object overhead for the
    // PSI sizes in this artifact.  Logs from the 128-bit NTT runs show Horner
    // around 16K targets is still much faster than switching at 32K, so keep
    // the default on the empirically better path unless callers override it.
    const char* env = std::getenv("CG_POLY_HORNER_MAX_POINTS");
    if (!env || !*env) return 65536;
    char* end = nullptr;
    unsigned long v = std::strtoul(env, &end, 10);
    if (end == env) return 65536;
    return (size_t)v;
}

inline std::string poly_eval_method_env()
{
    // Optional override: auto, ntt, horner, multipoint, or product_tree.
    const char* env = std::getenv("CG_POLY_EVAL_METHOD");
    return (env && *env) ? std::string(env) : std::string("auto");
}

inline std::vector<BICYCL::Mpz> poly_eval_batch_auto(
    const std::vector<BICYCL::Mpz>& coeffs,
    const std::vector<BICYCL::Mpz>& points,
    const BICYCL::Mpz& q)
{
    // Select the polynomial evaluation strategy used by OPA/PSI.
    const std::string method = poly_eval_method_env();
    if (method == "ntt") {
        if (!is_public_eval_points(points, q))
            throw std::runtime_error("CG_POLY_EVAL_METHOD=ntt requires public NTT points");
        return poly_eval_batch_ntt(coeffs, points.size(), q);
    }
    if (method == "horner")
        return poly_eval_batch_horner(coeffs, points, q);
#if defined(CG_USE_NTL_POLY)
    if (method == "ntl")
        return poly_eval_batch_ntl(coeffs, points, q);
#endif
    if (method == "multipoint" || method == "product_tree")
        return poly_eval_batch_product_tree(coeffs, points, q);
    if (method != "auto")
        std::cerr << "[poly_eval] unknown CG_POLY_EVAL_METHOD='" << method
                  << "', using auto\n";

    if (ntt_domain_supported(q, points.size()) && coeffs.size() <= points.size()
        && is_public_eval_points(points, q))
        return poly_eval_batch_ntt(coeffs, points.size(), q);

    if (points.size() <= poly_eval_auto_horner_max_points())
        return poly_eval_batch_horner(coeffs, points, q);
#if defined(CG_USE_NTL_POLY)
    if (ntl_poly_enabled() && std::max(coeffs.size(), points.size()) >= ntl_poly_threshold())
        return poly_eval_batch_ntl(coeffs, points, q);
#endif
    return poly_eval_batch_product_tree(coeffs, points, q);
}

inline std::vector<BICYCL::Mpz> rf_opa_eval_points(
    size_t n_pts,
    const BICYCL::Mpz& q)
{
    const auto& alpha = public_eval_points_cached(n_pts, q);
    return std::vector<BICYCL::Mpz>(alpha.begin(), alpha.end());
}

inline std::vector<BICYCL::Mpz> rf_opa_eval_points(size_t n_pts)
{
    std::vector<BICYCL::Mpz> alpha(n_pts);
    for (size_t i = 0; i < n_pts; ++i)
        alpha[i] = BICYCL::Mpz((unsigned long)(i + 1));
    return alpha;
}

inline size_t psi_opa_point_count(size_t degree_bound_plus_one, const BICYCL::Mpz& q)
{
    if (ntt_runtime_enabled() && is_ntt128_prime(q))
        return next_power_of_two_size(degree_bound_plus_one);
    return degree_bound_plus_one;
}

inline void require_public_eval_points_distinct(size_t n_pts, const BICYCL::Mpz& q)
{
    // Guard against small-q tests where alpha_i = i+1 would collide modulo q,
    // making Lagrange denominators non-invertible.
    if (n_pts == 0)
        return;
    BICYCL::Mpz n_mpz((unsigned long)n_pts);
    if (n_mpz > q) {
        std::ostringstream oss;
        oss << "public OPA/Lagrange point count n_pts=" << n_pts
            << " exceeds plaintext field size q=" << q
            << "; alpha_i=i+1 would repeat modulo q";
        throw std::runtime_error(oss.str());
    }
}

inline std::vector<BICYCL::Mpz> poly_derivative_mod(
    const std::vector<BICYCL::Mpz>& p,
    const BICYCL::Mpz& q)
{
    // Formal derivative in Z_q.
    if (p.size() <= 1) return {BICYCL::Mpz(0UL)};
    std::vector<BICYCL::Mpz> d(p.size() - 1, BICYCL::Mpz(0UL));
    for (size_t i = 1; i < p.size(); ++i) {
        BICYCL::Mpz::mul(d[i - 1], p[i], BICYCL::Mpz((unsigned long)i));
        BICYCL::Mpz::mod(d[i - 1], d[i - 1], q);
    }
    poly_trim(d);
    return d;
}

inline std::vector<BICYCL::Mpz> lagrange_interpolate_from_tree(
    const std::vector<BICYCL::Mpz>& y,
    const PolyProductTree& tree,
    const BICYCL::Mpz& q)
{
    // Interpolate coefficients from values on the tree's leaf points.
    const size_t N = y.size();
    if (N == 0) return {BICYCL::Mpz(0UL)};
    if (tree.levels.empty() || tree.levels[0].size() != N)
        throw std::runtime_error("lagrange_interpolate_from_tree: bad tree");

    const std::vector<BICYCL::Mpz>& P = tree.levels.back().front();
    std::vector<BICYCL::Mpz> P_deriv = poly_derivative_mod(P, q);
    std::vector<BICYCL::Mpz> denom = poly_eval_batch_product_tree(P_deriv, rf_opa_eval_points(N, q), q);

    std::vector<std::vector<BICYCL::Mpz>> acc(N);
    global_pool().parallel_for(0, N, [&](size_t i) {
        BICYCL::Mpz inv;
        BICYCL::Mpz::mod_inverse(inv, denom[i], q);
        BICYCL::Mpz wi;
        BICYCL::Mpz::mul(wi, y[i], inv);
        BICYCL::Mpz::mod(wi, wi, q);
        acc[i] = {wi};
    }, 64);

    for (size_t level = 0; level + 1 < tree.levels.size(); ++level) {
        const auto& polys = tree.levels[level];
        std::vector<std::vector<BICYCL::Mpz>> next((acc.size() + 1) / 2);
        global_pool().parallel_for(0, next.size(), [&](size_t i) {
            const size_t li = 2 * i;
            const size_t ri = li + 1;
            if (ri >= acc.size()) {
                next[i] = std::move(acc[li]);
                return;
            }
            std::vector<BICYCL::Mpz> left = poly_mul_mod(acc[li], polys[ri], q);
            std::vector<BICYCL::Mpz> right = poly_mul_mod(acc[ri], polys[li], q);
            next[i] = poly_add_mod(left, right, q);
        });
        acc = std::move(next);
    }
    return acc.front();
}

inline std::vector<BICYCL::Mpz> lagrange_interpolate_public_points(
    const std::vector<BICYCL::Mpz>& y,
    const BICYCL::Mpz& q)
{
    // Interpolate from values at the active public points.  For the fixed
    // 128-bit NTT prime and power-of-two PSI domains this is just an inverse NTT.
    if (ntt_domain_supported(q, y.size()))
        return lagrange_interpolate_ntt(y, q);
#if defined(CG_USE_NTL_POLY)
    if (ntl_poly_enabled() && y.size() >= ntl_poly_threshold()) {
        NTL::ZZ_pPush push(ntl_zz_from_mpz(q));
        NTL::ZZ_pX p;
        NTL::interpolate(p,
                         ntl_vec_from_mpz_vec(rf_opa_eval_points(y.size(), q)),
                         ntl_vec_from_mpz_vec(y));
        return mpz_vec_from_ntl_poly(p);
    }
#endif
    return lagrange_interpolate_from_tree(
        y, public_product_tree_cached(y.size(), q), q);
}

inline std::string lagrange_denominator_cache_key(size_t n, const BICYCL::Mpz& q)
{
    // Cache key because denominator inverses depend only on N and q.
    return public_poly_cache_key(n, q);
}

inline const std::vector<BICYCL::Mpz>& lagrange_denominator_inverses(
    size_t N,
    const BICYCL::Mpz& q)
{
    // Return cached inverses of public-point Lagrange denominators.
    require_public_eval_points_distinct(N, q);
    static std::mutex cache_mu;
    static std::unordered_map<std::string, std::vector<BICYCL::Mpz>> cache;

    const std::string key = lagrange_denominator_cache_key(N, q);
    std::lock_guard<std::mutex> lock(cache_mu);
    auto found = cache.find(key);
    if (found != cache.end())
        return found->second;

    // For public points alpha_i = i+1, the Lagrange denominator is:
    // D_i = prod_{j != i} ((i+1) - (j+1))
    //     = i! * (-1)^(N-1-i) * (N-1-i)!.
    // Its inverse depends only on N and q, so PSI can reuse it across calls.
    std::vector<BICYCL::Mpz> fact(N, BICYCL::Mpz(1UL));
    for (size_t i = 1; i < N; ++i) {
        BICYCL::Mpz::mul(fact[i], fact[i - 1], BICYCL::Mpz((unsigned long)i));
        BICYCL::Mpz::mod(fact[i], fact[i], q);
    }

    std::vector<BICYCL::Mpz> inv(N);
    for (size_t i = 0; i < N; ++i) {
        BICYCL::Mpz D;
        BICYCL::Mpz::mul(D, fact[i], fact[N - 1 - i]);
        BICYCL::Mpz::mod(D, D, q);
        if ((N - 1 - i) % 2 != 0)
            BICYCL::Mpz::sub(D, q, D);
        BICYCL::Mpz::mod_inverse(inv[i], D, q);
    }

    auto inserted = cache.emplace(key, std::move(inv));
    return inserted.first->second;
}

// Fast Barycentric Lagrange Interpolation for alpha_i = i + 1.
// Denominator inverses are cached by (N, q); each call only multiplies by y_i
// and evaluates at the requested targets.
inline std::vector<BICYCL::Mpz> lagrange_eval_batch(
    const std::vector<BICYCL::Mpz>& y,
    const std::vector<BICYCL::Mpz>& targets,
    const BICYCL::Mpz& q)
{
    // Evaluate the unique polynomial defined by y_i at alpha_i=i+1 directly at
    // target points, without materializing all coefficients.
    size_t N = y.size();
    std::vector<BICYCL::Mpz> result(targets.size(), BICYCL::Mpz(0UL));
    if (N == 0) return result;

    const std::vector<BICYCL::Mpz>& D_inv = lagrange_denominator_inverses(N, q);
    std::vector<BICYCL::Mpz> W(N);
    for (size_t i = 0; i < N; ++i) {
        BICYCL::Mpz::mul(W[i], y[i], D_inv[i]);
        BICYCL::Mpz::mod(W[i], W[i], q);
    }

    // Evaluate each target independently.
    global_pool().parallel_for(0, targets.size(), [&](size_t t) {
        const BICYCL::Mpz& xi = targets[t];
        
        // L[i] = prod_{j=0}^{i-1} (xi - (j+1))
        std::vector<BICYCL::Mpz> L(N, BICYCL::Mpz(1UL));
        BICYCL::Mpz cur(1UL);
        for (size_t i = 0; i < N - 1; ++i) {
            BICYCL::Mpz term;
            BICYCL::Mpz::sub(term, xi, BICYCL::Mpz((unsigned long)(i + 1)));
            BICYCL::Mpz::mod(term, term, q);
            BICYCL::Mpz::mul(cur, cur, term);
            BICYCL::Mpz::mod(cur, cur, q);
            L[i+1] = cur;
        }

        // R[i] = prod_{j=i+1}^{N-1} (xi - (j+1))
        std::vector<BICYCL::Mpz> R(N, BICYCL::Mpz(1UL));
        cur = BICYCL::Mpz(1UL);
        for (int i = (int)N - 1; i > 0; --i) {
            BICYCL::Mpz term;
            BICYCL::Mpz::sub(term, xi, BICYCL::Mpz((unsigned long)(i + 1)));
            BICYCL::Mpz::mod(term, term, q);
            BICYCL::Mpz::mul(cur, cur, term);
            BICYCL::Mpz::mod(cur, cur, q);
            R[i-1] = cur;
        }

        BICYCL::Mpz res(0UL);
        for (size_t i = 0; i < N; ++i) {
            BICYCL::Mpz term;
            BICYCL::Mpz::mul(term, W[i], L[i]);
            BICYCL::Mpz::mod(term, term, q);
            BICYCL::Mpz::mul(term, term, R[i]);
            BICYCL::Mpz::mod(term, term, q);
            
            BICYCL::Mpz::add(res, res, term);
            BICYCL::Mpz::mod(res, res, q);
        }
        result[t] = res;
    });
    return result;
}

inline size_t psi_lagrange_to_coeff_threshold()
{
    // PSI tuning knob: above this size, interpolate once to coefficients and
    // use batch polynomial evaluation instead of direct barycentric evaluation.
    const char* env = std::getenv("CG_PSI_INTERP_TO_COEFF_THRESHOLD");
    if (!env || !*env) return 4096;
    char* end = nullptr;
    unsigned long v = std::strtoul(env, &end, 10);
    return (end == env) ? 4096 : (size_t)v;
}

inline std::vector<BICYCL::Mpz> lagrange_eval_batch_auto(
    const std::vector<BICYCL::Mpz>& y,
    const std::vector<BICYCL::Mpz>& targets,
    const BICYCL::Mpz& q)
{
    // Select PSI reconstruction strategy: direct barycentric for smaller jobs,
    // coefficient interpolation plus multipoint evaluation for larger jobs.
    const size_t work = y.size() * targets.size();
    const size_t threshold = psi_lagrange_to_coeff_threshold();
    if (y.size() == 0 || targets.size() == 0)
        return std::vector<BICYCL::Mpz>(targets.size(), BICYCL::Mpz(0UL));

    if (ntt_domain_supported(q, y.size())) {
        auto t0 = Clock::now();
        std::vector<BICYCL::Mpz> coeffs = lagrange_interpolate_ntt(y, q);
        std::cerr << "[psi_poly] inverse NTT interpolated public-point values to "
                  << coeffs.size() << " coefficients in " << ms_since(t0) << " ms\n";

        t0 = Clock::now();
        std::vector<BICYCL::Mpz> out = poly_eval_batch_auto(coeffs, targets, q);
        std::cerr << "[psi_poly] evaluated NTT-interpolated polynomial at "
                  << targets.size() << " PSI elements in " << ms_since(t0) << " ms\n";
        return out;
    }

    if (std::max(y.size(), targets.size()) < threshold)
        return lagrange_eval_batch(y, targets, q);

    auto t0 = Clock::now();
    std::vector<BICYCL::Mpz> coeffs = lagrange_interpolate_public_points(y, q);
    std::cerr << "[psi_poly] interpolated public-point values to "
              << coeffs.size() << " coefficients in " << ms_since(t0) << " ms\n";

    t0 = Clock::now();
    std::vector<BICYCL::Mpz> out = poly_eval_batch_auto(coeffs, targets, q);
    std::cerr << "[psi_poly] evaluated coefficient polynomial at "
              << targets.size() << " PSI elements in " << ms_since(t0) << " ms"
              << " (nominal barycentric work avoided=" << work << " terms)\n";
    return out;
}

// Random polynomial of given degree with coefficients in Z_q
inline std::vector<BICYCL::Mpz> random_poly(
    size_t degree,
    const BICYCL::Mpz& q,
    BICYCL::RandGen& rng)
{
    // Sample a dense random polynomial of exactly degree slots degree+1.  The
    // leading coefficient may be zero; callers use it as a random mask vector.
    std::vector<BICYCL::Mpz> p(degree + 1);
    for (auto& c : p) {
        // Each coefficient is sampled uniformly modulo the plaintext bound q.
        c = rng.random_mpz(q);
    }
    return p;
}

inline void sample_unique_field_elements(
    size_t count,
    const BICYCL::Mpz& q,
    BICYCL::RandGen& rng,
    std::vector<BICYCL::Mpz>& out,
    std::unordered_set<std::string>& seen)
{
    // Sample nonzero unique benchmark set elements, honoring the configured
    // input-size bound.  "seen" is string-based to make Mpz hashing unnecessary.
    out.reserve(out.size() + count);
    BICYCL::Mpz bound = benchmark_input_bound(q);
    while (count > 0) {
        BICYCL::Mpz x = rng.random_mpz(bound);
        if (x == BICYCL::Mpz(0UL)) continue;
        std::ostringstream oss;
        oss << x;
        if (seen.insert(oss.str()).second) {
            out.push_back(x);
            --count;
        }
    }
}

inline void make_random_psi_sets_full_field(
    size_t mA,
    size_t mB,
    uint64_t seed,
    const BICYCL::Mpz& q,
    std::vector<BICYCL::Mpz>& setA,
    std::vector<BICYCL::Mpz>& setB)
{
    // Deterministically generate two benchmark PSI sets with 20% overlap.  This
    // is for local/simulation paths; two-machine scripts normally generate and
    // pass explicit input files.
    BICYCL::RandGen shared_rng = make_seeded_benchmark_randgen(seed, 0x5053495f53484152ULL);
    BICYCL::RandGen rngA = make_seeded_benchmark_randgen(seed, 0x5053495f415f5f5fULL);
    BICYCL::RandGen rngB = make_seeded_benchmark_randgen(seed, 0x5053495f425f5f5fULL);

    std::unordered_set<std::string> seenA, seenB, seenAll;
    const size_t overlap = std::min(mA, mB) / 5;
    std::vector<BICYCL::Mpz> shared;
    sample_unique_field_elements(overlap, q, shared_rng, shared, seenAll);
    for (const auto& x : shared) {
        std::ostringstream oss;
        oss << x;
        seenA.insert(oss.str());
        seenB.insert(oss.str());
        setA.push_back(x);
        setB.push_back(x);
    }

    sample_unique_field_elements(mA - overlap, q, rngA, setA, seenA);
    sample_unique_field_elements(mB - overlap, q, rngB, setB, seenB);
}
