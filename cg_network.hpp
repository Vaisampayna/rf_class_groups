#pragma once
/*
 * Minimal TCP and serialization layer for CG benchmark artifacts.
 *
 * Provides exact-length reads/writes, retrying TCP connects for two-process
 * launches, and stable serialization of Mpz/public-key/ciphertext objects.
 */

#include <cstdint>
#include <cstring>
#include <cerrno>
#include <cstdlib>
#include <stdexcept>
#include <string>
#include <sstream>
#include <vector>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/tcp.h>

#include "cg_ahe/cg_ahe.hpp"

namespace CGNet {

inline void set_tcp_opts(int fd) {
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
    int sz = 4 * 1024 * 1024;
    setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sz, sizeof(sz));
    setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &sz, sizeof(sz));
}

// ── Raw I/O ───────────────────────────────────────────────────────────────
inline void send_all(int fd, const void* buf, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(buf);
    while (len > 0) {
        // write() can complete partially, so keep sending until the serialized
        // field is fully written.
        ssize_t w = write(fd, p, len);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) throw std::runtime_error(std::string("write: ") + strerror(errno));
        p += w; len -= w;
    }
}
inline void recv_all(int fd, void* buf, size_t len) {
    uint8_t* p = static_cast<uint8_t*>(buf);
    while (len > 0) {
        // read() can return fewer bytes than requested; protocol decoding needs
        // exact field lengths.
        ssize_t r = read(fd, p, len);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) throw std::runtime_error(std::string("read: ") + strerror(errno));
        p += r; len -= r;
    }
}

// ── Integer I/O ───────────────────────────────────────────────────────────
inline void send_u32(int fd, uint32_t v) {
    uint32_t n = htonl(v); send_all(fd, &n, 4);
}
inline uint32_t recv_u32(int fd) {
    uint32_t n; recv_all(fd, &n, 4); return ntohl(n);
}
inline void send_u64(int fd, uint64_t v) {
    // Transfer 64-bit counts as two portable network-order 32-bit words.
    send_u32(fd, (uint32_t)(v >> 32));
    send_u32(fd, (uint32_t)(v & 0xFFFFFFFFULL));
}
inline uint64_t recv_u64(int fd) {
    uint64_t hi = recv_u32(fd), lo = recv_u32(fd);
    return (hi << 32) | lo;
}

// ── TCP helpers ───────────────────────────────────────────────────────────
inline int connect_tcp_retry(const char* host, const char* port, int retries = 0) {
    if (retries <= 0) {
        const char* env = std::getenv("CG_CONNECT_RETRIES");
        retries = (env && *env) ? std::atoi(env) : 3600;
        if (retries <= 0) retries = 3600;
    }
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    for (int i = 0; i < retries; ++i) {
        // Demo scripts start processes separately, so retry while the peer is
        // still coming up.
        if (getaddrinfo(host, port, &hints, &res) == 0) {
            for (auto* ai = res; ai; ai = ai->ai_next) {
                int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
                if (fd < 0) continue;
                if (connect(fd, ai->ai_addr, ai->ai_addrlen) == 0) {
                    set_tcp_opts(fd);
                    freeaddrinfo(res); return fd;
                }
                close(fd);
            }
            freeaddrinfo(res); res = nullptr;
        }
        usleep(200000);
    }
    throw std::runtime_error(std::string("connect_tcp failed: ") + host + ":" + port);
}

inline int listen_tcp(const char* port) {
    struct addrinfo hints{}, *res = nullptr;
    // Bind a passive IPv4/IPv6 TCP socket on the requested local port.
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags    = AI_PASSIVE;
    if (getaddrinfo(nullptr, port, &hints, &res) != 0)
        throw std::runtime_error("getaddrinfo failed");
    int fd = -1;
    for (auto* ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        int yes = 1;
        // Permit immediate reruns of local demos on the same port.
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(fd, ai->ai_addr, ai->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) throw std::runtime_error("bind failed");
    if (listen(fd, SOMAXCONN) != 0) throw std::runtime_error("listen failed");
    return fd;
}

inline int accept_one(int lfd) {
    struct sockaddr_storage peer; socklen_t plen = sizeof(peer);
    int cfd;
    do {
        cfd = accept(lfd, (struct sockaddr*)&peer, &plen);
    } while (cfd < 0 && errno == EINTR);
    if (cfd < 0) throw std::runtime_error("accept failed");
    set_tcp_opts(cfd);
    return cfd;
}

// ── Mpz I/O (using BICYCL stream operators — no get_mpz_t()) ─────────────
inline void send_mpz(int fd, const BICYCL::Mpz& m) {
    // Use GMP srcptr cast for read-only export
    mpz_srcptr raw = (mpz_srcptr)m;
    size_t count = 0;
    // Reserve enough bytes for the absolute value and export it big-endian.
    std::vector<uint8_t> buf(mpz_sizeinbase(raw, 2) / 8 + 2);
    mpz_export(buf.data(), &count, 1, 1, 1, 0, raw);
    // Wire format: sign flag, byte count, then magnitude bytes.
    send_u32(fd, (m.sgn() < 0) ? 1u : 0u);
    send_u32(fd, (uint32_t)count);
    if (count) send_all(fd, buf.data(), count);
}
inline BICYCL::Mpz recv_mpz(int fd) {
    uint32_t neg   = recv_u32(fd);
    uint32_t count = recv_u32(fd);
    BICYCL::Mpz m(0UL);
    if (count) {
        std::vector<uint8_t> buf(count);
        recv_all(fd, buf.data(), count);
        std::vector<unsigned char> ubuf(buf.begin(), buf.end());
        m = ubuf;
    }
    if (neg) m.neg();
    return m;
}

// ── QFI I/O ───────────────────────────────────────────────────────────────
inline void send_qfi(int fd, const BICYCL::QFI& q) {
    // A QFI/class-group element is serialized as its three integer fields.
    send_mpz(fd, q.a());
    send_mpz(fd, q.b());
    send_mpz(fd, q.c());
}
inline BICYCL::QFI recv_qfi(int fd) {
    BICYCL::Mpz a = recv_mpz(fd);
    BICYCL::Mpz b = recv_mpz(fd);
    BICYCL::Mpz c = recv_mpz(fd);
    return BICYCL::QFI(a, b, c);
}

// ── CG-AHE type I/O (CG_AHE:: namespace from cg_ahe.hpp) ─────────────────
inline void send_pk(int fd, const CG_AHE::PublicKey& pk) {
    send_qfi(fd, pk.elt());
}
inline CG_AHE::PublicKey recv_pk(int fd, const BICYCL::CL_HSMqk& cs) {
    return CG_AHE::PublicKey(cs, recv_qfi(fd));
}
inline void send_ct(int fd, const CG_AHE::CipherText& ct) {
    // Ciphertext = pair of class-group elements.
    send_qfi(fd, ct.c1());
    send_qfi(fd, ct.c2());
}
inline CG_AHE::CipherText recv_ct(int fd) {
    BICYCL::QFI c1 = recv_qfi(fd);
    BICYCL::QFI c2 = recv_qfi(fd);
    return CG_AHE::CipherText(c1, c2);
}

} // namespace CGNet
