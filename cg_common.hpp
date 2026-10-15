/**
 * cg_common.hpp — Shared utilities for CG-AHE Reverse Firewall OLE/OPA/PSI
 *
 * Provides:
 *   - Wall-clock timer
 *   - Low-level socket send/recv helpers
 *   - BICYCL::Mpz and QFI serialisation for the socket helpers
 *   - CG-AHE CipherText / PublicKey send/recv
 */
#pragma once

#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

// BICYCL + our wrapper
#include "cg_config.hpp"
#include "cg_network.hpp"
#include "cg_ahe/bicycl.hpp"
#include "cg_ahe/cg_ahe.hpp"

using namespace BICYCL;
using namespace CG_AHE;

// Defaults for the standalone single-OLE binaries. CG_Q_NBITS=128 is the
// setting used for the reported benchmark timings.
static inline size_t cg_common_env_size(const char* name, size_t fallback) {
    return cg_config_size(name, fallback, 4096);
}
static const size_t CG_COMMON_Q_NBITS = cg_common_env_size("CG_Q_NBITS", 256);
static const size_t CG_COMMON_K = cg_common_env_size("CG_K", 1);
static const SecLevel CG_COMMON_SECLEVEL = SecLevel::_128;

static inline Mpz mpz_from_bytes_common(const std::vector<unsigned char>& bytes) {
    Mpz x;
    x = bytes;
    return x;
}

static inline RandGen make_seeded_common_randgen(uint64_t seed, uint64_t domain) {
    std::vector<unsigned char> bytes(32, 0);
    uint64_t x = seed ^ (0x9e3779b97f4a7c15ULL + (domain << 1));
    for (size_t i = 0; i < bytes.size(); ++i) {
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        uint64_t z = x * 0x2545f4914f6cdd1dULL;
        bytes[i] = (unsigned char)(z >> ((i % 8) * 8));
    }
    RandGen rng;
    rng.set_seed(mpz_from_bytes_common(bytes));
    return rng;
}

static inline uint64_t cg_common_public_seed() {
    const char* env = getenv("CG_PUBLIC_SEED");
    if (!env || !*env) return 0x43475f5055424c49ULL;
    return cg_parse_u64(env, "CG_PUBLIC_SEED");
}

static inline RandGen make_common_public_param_randgen() {
    return make_seeded_common_randgen(cg_common_public_seed(),
                                      0x5055425f5041524dULL);
}

static inline CG_Scheme make_common_cg_scheme(RandGen& runtime_rng) {
    // The class-group public parameters must be identical in every process
    // that serializes/deserializes keys or ciphertexts. Runtime randomness
    // remains independent and CSPRNG-seeded.
    RandGen public_rng = make_common_public_param_randgen();
    return CG_Scheme(CG_COMMON_Q_NBITS, CG_COMMON_K, CG_COMMON_SECLEVEL,
                     public_rng, runtime_rng);
}

// ── Timing ───────────────────────────────────────────────────────────────────
static inline double wall_now_s() {
    struct timespec ts;
    // Use a monotonic clock so elapsed timings are not affected by wall-clock
    // changes such as NTP updates.
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

// ── Fatal error helpers ───────────────────────────────────────────────────────
static inline void die(const char *role, const char *msg) {
    fprintf(stderr, "\n[%s] FATAL: %s\n", role, msg);
    exit(1);
}
static inline void die_errno(const char *role, const char *msg) {
    fprintf(stderr, "\n[%s] FATAL: %s: %s\n", role, msg, strerror(errno));
    exit(1);
}

// ── Low-level byte send/recv ──────────────────────────────────────────────────
static inline void send_all(const char *role, int fd, const void *buf, size_t len) {
    CGNet::send_all(fd, buf, len);
}
static inline void recv_all(const char *role, int fd, void *buf, size_t len) {
    CGNet::recv_all(fd, buf, len);
}
static inline void send_u32(const char *r, int fd, uint32_t v)
    { uint32_t nv = htonl(v); send_all(r, fd, &nv, 4); }
static inline uint32_t recv_u32(const char *r, int fd)
    { uint32_t nv; recv_all(r, fd, &nv, 4); return ntohl(nv); }
static inline void send_u64(const char *r, int fd, uint64_t v) {
    // Split 64-bit values into two network-order 32-bit words. This avoids
    // relying on non-standard htonll helpers.
    send_u32(r, fd, (uint32_t)(v >> 32));
    send_u32(r, fd, (uint32_t)(v & 0xFFFFFFFFULL));
}
static inline uint64_t recv_u64(const char *r, int fd) {
    uint64_t hi = recv_u32(r, fd), lo = recv_u32(r, fd);
    return (hi << 32) | lo;
}

// ── Mpz serialisation ─────────────────────────────────────────────────────────
// Format matches CGNet in cg_network.hpp:
//   [4-byte sign: 0=nonnegative, 1=negative]
//   [4-byte count][count bytes, MSB-first magnitude]
static inline void send_mpz(const char *role, int fd, const Mpz &m) {
    CGNet::send_mpz(fd, m);
}
static inline Mpz recv_mpz(const char *role, int fd) {
    return CGNet::recv_mpz(fd);
}

// ── QFI serialisation (a, b, c components) ───────────────────────────────────
static inline void send_qfi(const char *role, int fd, const QFI &q) {
    // A binary quadratic form is represented by the three integers (a,b,c).
    send_mpz(role, fd, q.a());
    send_mpz(role, fd, q.b());
    send_mpz(role, fd, q.c());
}
static inline QFI recv_qfi(const char *role, int fd) {
    Mpz a = recv_mpz(role, fd);
    Mpz b = recv_mpz(role, fd);
    Mpz c = recv_mpz(role, fd);
    return QFI(a, b, c);
}

// ── CipherText send/recv ──────────────────────────────────────────────────────
static inline void send_ct(const char *role, int fd, const CipherText &ct) {
    // CG-AHE ciphertexts are pairs of class-group elements.
    send_qfi(role, fd, ct.c1());
    send_qfi(role, fd, ct.c2());
}
static inline CipherText recv_ct(const char *role, int fd) {
    QFI c1 = recv_qfi(role, fd);
    QFI c2 = recv_qfi(role, fd);
    return CipherText(c1, c2);
}

// ── PublicKey send/recv ───────────────────────────────────────────────────────
static inline void send_pk(const char *role, int fd, const PublicKey &pk) {
    send_qfi(role, fd, pk.elt());
}
static inline PublicKey recv_pk(const char *role, int fd, const CS &cs) {
    QFI q = recv_qfi(role, fd);
    return PublicKey(cs, q);
}

// ── Seed BICYCL RandGen from /dev/urandom ────────────────────────────────────
static inline void seed_randgen(RandGen &rng) {
    std::vector<unsigned char> buf(32);
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        if (fread(buf.data(), 1, 32, f) != 32) {
            die("rng", "short read from /dev/urandom");
        }
        fclose(f);
    } else {
        die("rng", "cannot open /dev/urandom");
    }
    Mpz seed;
    seed = buf;   // Mpz::operator=(const vector<unsigned char>&)
    rng.set_seed(seed);
}

// ── TCP helpers ───────────────────────────────────────────────────────────────
static inline void set_sockopts(const char *role, int fd) {
    int yes = 1;
    if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes)) != 0)
        die_errno(role, "setsockopt TCP_NODELAY");
}
static inline int listen_tcp(const char *role, const char *port) {
    return CGNet::listen_tcp(port);
}
static inline int accept_tcp(const char *role, int lfd) {
    return CGNet::accept_one(lfd);
}
static inline int connect_tcp(const char *role, const char *host,
                               const char *port, int wait_sec = 120) {
    return CGNet::connect_tcp_retry(host, port, wait_sec * 5);
}

// ── Small-integer deterministic hash (for reproducible test inputs) ───────────
static inline uint32_t mix32(uint32_t x) {
    // Fast deterministic mixing for repeatable benchmark inputs, not a crypto hash.
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
    x *= 0x846ca68bu; x ^= x >> 16; return x;
}
static inline uint32_t sample16(uint32_t seed, uint32_t i) {
    // Produce small benchmark values while keeping every run reproducible.
    return mix32(seed + i * 0x9e3779b9u) & 0xFFFFu;
}
