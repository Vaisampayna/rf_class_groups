/**
 * cg_common.hpp — Shared utilities for CG-AHE Reverse Firewall OLE/OPA/PSI
 *
 * Provides:
 *   - Wall-clock timer
 *   - Low-level socket send/recv helpers
 *   - BICYCL::Mpz and QFI serialisation (same format as Paillier lan_common.h)
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
#include "cg_ahe/bicycl.hpp"
#include "cg_ahe/cg_ahe.hpp"

using namespace BICYCL;
using namespace CG_AHE;

// Production defaults for the legacy single-OLE binaries. Use
// CG_Q_NBITS=128 is the setting used for the reported benchmark timings.
static inline size_t cg_common_env_size(const char* name, size_t fallback) {
    const char* env = getenv(name);
    if (!env || !*env) return fallback;
    char* end = nullptr;
    unsigned long v = strtoul(env, &end, 10);
    return (end == env || v == 0) ? fallback : (size_t)v;
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
    char* end = nullptr;
    unsigned long long v = strtoull(env, &end, 10);
    return (end == env) ? 0x43475f5055424c49ULL : (uint64_t)v;
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
    const uint8_t *p = reinterpret_cast<const uint8_t*>(buf);
    while (len > 0) {
        // send() may write only part of the buffer, so keep advancing until the
        // complete protocol field has been transmitted.
        ssize_t n = send(fd, p, len, 0);
        if (n < 0) { if (errno == EINTR) continue; die_errno(role, "send"); }
        if (n == 0) die(role, "send returned 0");
        p += n; len -= n;
    }
}
static inline void recv_all(const char *role, int fd, void *buf, size_t len) {
    uint8_t *p = reinterpret_cast<uint8_t*>(buf);
    while (len > 0) {
        // recv() may return a short read. The higher-level serializers depend
        // on exact field sizes, so block until the requested number arrives.
        ssize_t n = recv(fd, p, len, 0);
        if (n < 0) { if (errno == EINTR) continue; die_errno(role, "recv"); }
        if (n == 0) die(role, "connection closed unexpectedly");
        p += n; len -= n;
    }
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
    mpz_srcptr raw = (mpz_srcptr)m;
    size_t count = 0;
    std::vector<unsigned char> bytes(mpz_sizeinbase(raw, 2) / 8 + 2);
    mpz_export(bytes.data(), &count, 1, 1, 1, 0, raw);
    send_u32(role, fd, (m.sgn() < 0) ? 1u : 0u);
    send_u32(role, fd, count);
    if (count > 0) send_all(role, fd, bytes.data(), count);
}
static inline Mpz recv_mpz(const char *role, int fd) {
    uint32_t neg = recv_u32(role, fd);
    uint32_t count = recv_u32(role, fd);
    Mpz m(0UL);
    if (count > 0) {
        std::vector<unsigned char> buf(count);
        recv_all(role, fd, buf.data(), count);
        m = buf;
    }
    if (neg) m.neg();
    return m;
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
    struct addrinfo hints{}, *res = nullptr;
    // Allow IPv4 or IPv6 and bind as a passive server socket.
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
    if (getaddrinfo(nullptr, port, &hints, &res) != 0) die(role, "getaddrinfo");
    int fd = -1;
    for (auto *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int yes = 1;
        // Make repeated benchmark runs less likely to fail on TIME_WAIT.
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) die_errno(role, "bind");
    if (listen(fd, 4) != 0) die_errno(role, "listen");
    return fd;
}
static inline int accept_tcp(const char *role, int lfd) {
    struct sockaddr_storage peer; socklen_t plen = sizeof(peer);
    int fd = accept(lfd, (struct sockaddr *)&peer, &plen);
    if (fd < 0) die_errno(role, "accept");
    set_sockopts(role, fd);
    return fd;
}
static inline int connect_tcp(const char *role, const char *host,
                               const char *port, int wait_sec = 120) {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) die(role, "getaddrinfo");
    for (int attempt = 0; attempt < wait_sec; ++attempt) {
        // The launcher scripts start four processes with sleeps between them;
        // retries make startup-order races harmless.
        for (auto *ai = res; ai; ai = ai->ai_next) {
            int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
            if (fd < 0) continue;
            if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
                set_sockopts(role, fd);
                freeaddrinfo(res);
                return fd;
            }
            close(fd);
        }
        sleep(1);
    }
    freeaddrinfo(res);
    die(role, "could not connect"); return -1;
}

// ── Small-integer deterministic hash (for reproducible test inputs) ───────────
static inline uint32_t mix32(uint32_t x) {
    // Fast deterministic mixing for repeatable toy inputs, not a crypto hash.
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15;
    x *= 0x846ca68bu; x ^= x >> 16; return x;
}
static inline uint32_t sample16(uint32_t seed, uint32_t i) {
    // Produce small benchmark values while keeping every run reproducible.
    return mix32(seed + i * 0x9e3779b9u) & 0xFFFFu;
}
