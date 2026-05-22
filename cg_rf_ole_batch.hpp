/**
 * cg_rf_ole_batch.hpp — Batched RF-OLE wrappers (persistent thread pool)
 *
 * KEY FIX: All parallel_for calls reuse the same worker threads from global_pool().
 * thread_local CG_Scheme (560ms each) is constructed exactly once per worker.
 * A prewarm_batch_pool() call at binary startup pays this cost before any I/O.
 */
#pragma once
/*
 * Transport-facing RF-OLE API.
 *
 * rf_ole_batch_receive() is the receiver endpoint, rf_ole_batch_send() is the
 * sender endpoint, and helper lane functions split ciphertext traffic across
 * deterministic TCP lanes.
 */

#include "cg_rf_common.hpp"
#include "cg_thread_pool.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cstdlib>
#include <thread>
#include <numeric>
#include <sys/socket.h>

inline void tune_socket_batch(int fd) {
    CGNet::set_tcp_opts(fd);
}

inline CG_AHE::CG_Scheme& worker_cg() {
    // Keep CG-AHE state thread-local.  BICYCL objects are expensive to
    // construct, so callers prewarm the pool once and then reuse these
    // per-worker scheme instances for all crypto-heavy loops.
    thread_local BICYCL::RandGen tl_public_rng = make_public_param_randgen();
    thread_local BICYCL::RandGen tl_rng = make_secure_randgen();
    thread_local CG_AHE::CG_Scheme tl_cg(CG_Q_NBITS, CG_K, CG_SECLEVEL,
                                         tl_public_rng, tl_rng);
    return tl_cg;
}

inline size_t rf_transport_lanes() {
    const char* env = std::getenv("CG_RF_LANES");
    if (!env || !*env) return 1;
    char* end = nullptr;
    unsigned long lanes = std::strtoul(env, &end, 10);
    if (end == env || lanes == 0) return 1;
    return (size_t)lanes;
}

inline size_t rf_chunk_size() {
    const char* env = std::getenv("CG_RF_CHUNK_SIZE");
    if (!env || !*env) return 64;
    char* end = nullptr;
    unsigned long chunk = std::strtoul(env, &end, 10);
    if (end == env || chunk == 0) return 64;
    return (size_t)chunk;
}

inline std::pair<size_t, size_t> rf_lane_range(size_t n, size_t lanes, size_t lane) {
    // Deterministic contiguous sharding: both endpoints compute the same
    // ciphertext range for each lane, so lane payloads need no per-item tags.
    size_t begin = (n * lane) / lanes;
    size_t end   = (n * (lane + 1)) / lanes;
    return {begin, end};
}

inline std::pair<size_t, size_t> rf_lane_range_intersection(
    size_t n,
    size_t lanes,
    size_t lane,
    size_t begin,
    size_t end)
{
    auto [lb, le] = rf_lane_range(n, lanes, lane);
    return {std::max(lb, begin), std::min(le, end)};
}

inline std::vector<int> accept_rf_lanes(int lfd, size_t lanes) {
    std::vector<int> fds(lanes, -1);
    for (size_t accepted = 0; accepted < lanes; ++accepted) {
        int fd = CGNet::accept_one(lfd);
        tune_socket_batch(fd);
        // TCP connections can be accepted in a different order from connect().
        // The initiator sends its lane id immediately so we can reorder fds.
        uint64_t lane_u64 = CGNet::recv_u64(fd);
        if (lane_u64 >= lanes || fds[(size_t)lane_u64] != -1) {
            close(fd);
            throw std::runtime_error("invalid or duplicate RF transport lane id");
        }
        fds[(size_t)lane_u64] = fd;
    }
    return fds;
}

inline std::vector<int> connect_rf_lanes(const char* host, const char* port, size_t lanes) {
    std::vector<int> fds(lanes);
    for (size_t lane = 0; lane < lanes; ++lane) {
        fds[lane] = CGNet::connect_tcp_retry(host, port);
        tune_socket_batch(fds[lane]);
        CGNet::send_u64(fds[lane], (uint64_t)lane);
    }
    return fds;
}

inline void close_rf_lanes(std::vector<int>& fds) {
    for (int fd : fds) close(fd);
    fds.clear();
}

inline double sum_ms(const std::vector<double>& xs) {
    return std::accumulate(xs.begin(), xs.end(), 0.0);
}

inline thread_local double g_last_rf_ole_online_ms = 0.0;

inline double last_rf_ole_online_ms() {
    return g_last_rf_ole_online_ms;
}

inline void send_ct_lanes(
    const std::vector<int>& fds,
    const std::vector<CG_AHE::CipherText>& values,
    size_t n)
{
    const size_t lanes = fds.size();
    std::vector<std::thread> workers;
    workers.reserve(lanes);
    for (size_t lane = 0; lane < lanes; ++lane) {
        workers.emplace_back([&, lane] {
            auto [begin, end] = rf_lane_range(n, lanes, lane);
            for (size_t i = begin; i < end; ++i)
                CGNet::send_ct(fds[lane], values[i]);
        });
    }
    for (auto& worker : workers) worker.join();
}

inline void send_ct_lanes_range(
    const std::vector<int>& fds,
    const std::vector<CG_AHE::CipherText>& values,
    size_t n,
    size_t begin,
    size_t end)
{
    const size_t lanes = fds.size();
    if (lanes == 1) {
        for (size_t i = begin; i < end; ++i)
            CGNet::send_ct(fds[0], values[i]);
        return;
    }

    // A chunk may touch only a few lane ranges.  Restricting work to those
    // lanes avoids spawning idle I/O threads on small chunks.
    std::vector<size_t> active_lanes;
    active_lanes.reserve(lanes);
    for (size_t lane = 0; lane < lanes; ++lane) {
        auto [b, e] = rf_lane_range_intersection(n, lanes, lane, begin, end);
        if (b < e) active_lanes.push_back(lane);
    }

    std::vector<std::thread> workers;
    workers.reserve(active_lanes.size());
    for (size_t lane : active_lanes) {
        workers.emplace_back([&, lane] {
            auto [b, e] = rf_lane_range_intersection(n, lanes, lane, begin, end);
            for (size_t i = b; i < e; ++i)
                CGNet::send_ct(fds[lane], values[i]);
        });
    }
    for (auto& worker : workers) worker.join();
}

inline void recv_ct_lanes(
    const std::vector<int>& fds,
    std::vector<CG_AHE::CipherText>& values,
    size_t n)
{
    const size_t lanes = fds.size();
    std::vector<std::thread> workers;
    workers.reserve(lanes);
    for (size_t lane = 0; lane < lanes; ++lane) {
        workers.emplace_back([&, lane] {
            auto [begin, end] = rf_lane_range(n, lanes, lane);
            for (size_t i = begin; i < end; ++i)
                values[i] = CGNet::recv_ct(fds[lane]);
        });
    }
    for (auto& worker : workers) worker.join();
}

inline void recv_ct_lanes_range(
    const std::vector<int>& fds,
    std::vector<CG_AHE::CipherText>& values,
    size_t n,
    size_t begin,
    size_t end)
{
    const size_t lanes = fds.size();
    if (lanes == 1) {
        for (size_t i = begin; i < end; ++i)
            values[i] = CGNet::recv_ct(fds[0]);
        return;
    }

    std::vector<size_t> active_lanes;
    active_lanes.reserve(lanes);
    for (size_t lane = 0; lane < lanes; ++lane) {
        auto [b, e] = rf_lane_range_intersection(n, lanes, lane, begin, end);
        if (b < e) active_lanes.push_back(lane);
    }

    std::vector<std::thread> workers;
    workers.reserve(active_lanes.size());
    for (size_t lane : active_lanes) {
        workers.emplace_back([&, lane] {
            auto [b, e] = rf_lane_range_intersection(n, lanes, lane, begin, end);
            for (size_t i = b; i < e; ++i)
                values[i] = CGNet::recv_ct(fds[lane]);
        });
    }
    for (auto& worker : workers) worker.join();
}

// Call once at main() startup — forces all workers to build their CG_Scheme.
inline void prewarm_batch_pool() {
    auto& pool = global_pool();
    auto t0 = std::chrono::high_resolution_clock::now();
    pool.parallel_for(0, pool.num_threads(), [](size_t) {
        (void)worker_cg();
    });
    double ms = std::chrono::duration<double,std::milli>(
        std::chrono::high_resolution_clock::now() - t0).count();
    std::cerr << "[batch_ole] pool pre-warmed in " << ms << " ms\n";
}

inline std::vector<BICYCL::Mpz> rf_ole_batch_receive(
    const std::vector<BICYCL::Mpz>& x_vals,
    const std::string& role,
    const char* port_rec = nullptr)
{
    // Receiver side of batched RF-OLE.  The public key and Enc(x_i) values are
    // sent through R-RF and S-RF in Round 1; encrypted OLE outputs return in
    // Round 2 and are decrypted locally.
    const size_t N = x_vals.size();
    const size_t LANES = rf_transport_lanes();
    const size_t CHUNK = rf_chunk_size();
    port_rec = port_rec ? port_rec : rf_port_rec();
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();

    CG_AHE::SecretKey sk = cg.keygen_sk();
    CG_AHE::PublicKey pk = cg.keygen_pk(sk);

    int lfd = CGNet::listen_tcp(port_rec);
    std::cerr << "[" << role << "] listening on :" << port_rec
              << " for " << LANES << " transport lane(s)\n";
    std::vector<int> fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    uint64_t n_rx = CGNet::recv_u64(fds[0]);
    if (n_rx != N) {
        std::cerr << "[" << role << "] OLE count mismatch: got=" << n_rx
                  << " expected=" << N << "\n";
        close_rf_lanes(fds);
        throw std::runtime_error("RF-OLE receiver count mismatch");
    }
    CGNet::send_pk(fds[0], pk);

    auto& pool = global_pool();
    size_t NT = pool.num_threads();

    // Encrypt and send in chunks so the firewalls can start processing early.
    std::cerr << "[" << role << "] [ENCRYPT AND SEND R1] Encrypting/sending "
              << N << " inputs across " << NT << " threads, chunk=" << CHUNK << "...\n";
    auto t_pre = std::chrono::high_resolution_clock::now();

    std::vector<CG_AHE::CipherText> pre_enc_x(N);
    std::vector<double> enc_ms(N, 0.0);
    std::cerr << "[" << role << "] [RECEIVE AND DECRYPT R2] Starting response stream...\n";
    auto t_online = std::chrono::high_resolution_clock::now();

    for (size_t begin = 0; begin < N; begin += CHUNK) {
        size_t end = std::min(N, begin + CHUNK);
        pool.parallel_for(begin, end, [&](size_t i) {
            // worker_cg() has the same deterministic public parameters as cg
            // (make_cg_scheme uses make_public_param_randgen()).  Keeping a
            // scheme per worker avoids sharing mutable BICYCL scratch state
            // while decrypting under the receiver's sk.
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            CG_AHE::ClearText x_ct(local_cg.cs(), x_vals[i]);
            auto op_t = Clock::now();
            pre_enc_x[i] = local_cg.encrypt(pk, x_ct);
            enc_ms[i] = ms_since(op_t);
        });
        send_ct_lanes_range(fds, pre_enc_x, N, begin, end);
    }
    double pre_ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t_pre).count();
    std::cerr << "[" << role << "] R1 " << N << "/" << N
              << " RF-OLE inputs sent over " << LANES << " lane(s)\n";
    std::cerr << "[" << role << "] encrypting and sending R1 ciphertexts done in " << pre_ms << " ms\n";
    std::cerr << "[" << role << "] [OPTIME] encrypt_x sum=" << sum_ms(enc_ms)
              << " ms, avg=" << (N ? sum_ms(enc_ms) / (double)N : 0.0)
              << " ms/op\n";

    // R2 is intentionally phase-separated from R1: both RF firewalls finish
    // the R1 key-mauling path before the sender's response path begins.
    std::vector<BICYCL::Mpz> y_vals(N);
    std::vector<CG_AHE::CipherText> y_cts(N);
    std::vector<double> dec_ms(N, 0.0);
    for (size_t begin = 0; begin < N; begin += CHUNK) {
        size_t end = std::min(N, begin + CHUNK);
        recv_ct_lanes_range(fds, y_cts, N, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            auto op_t = Clock::now();
            CG_AHE::ClearText y_ct = local_cg.decrypt(sk, y_cts[i]);
            dec_ms[i] = ms_since(op_t);
            y_vals[i] = static_cast<const BICYCL::Mpz&>(y_ct);
        });
    }
    std::cerr << "[" << role << "] R2 " << N << "/" << N
              << " RF-OLE outputs received\n";

    double online_ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t_online).count();
    g_last_rf_ole_online_ms = online_ms;
    std::cerr << "[" << role << "] full RF-OLE receive side stream done in " << online_ms << " ms\n";
    std::cerr << "[" << role << "] [OPTIME] decrypt_y sum=" << sum_ms(dec_ms)
              << " ms, avg=" << (N ? sum_ms(dec_ms) / (double)N : 0.0)
              << " ms/op\n";

    close_rf_lanes(fds);
    return y_vals;
}

inline void rf_ole_batch_send(
    const std::vector<BICYCL::Mpz>& a_vals,
    const std::vector<BICYCL::Mpz>& b_vals,
    const std::string& role,
    const char* port_rfs = nullptr)
{
    // Sender side of batched RF-OLE.  The sender only sees firewalled
    // ciphertexts/public keys and computes Enc(a_i*x_i+b_i) homomorphically.
    if (a_vals.size() != b_vals.size())
        throw std::runtime_error("RF-OLE sender vector size mismatch");
    const size_t N = a_vals.size();
    const size_t LANES = rf_transport_lanes();
    const size_t CHUNK = rf_chunk_size();
    port_rfs = port_rfs ? port_rfs : rf_port_rfs();

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();

    std::vector<int> fds = connect_rf_lanes(LOCALHOST, port_rfs, LANES);
    std::cerr << "[" << role << "] connected to RF_S with " << LANES
              << " lane(s), n_oles=" << N << "\n";

    CGNet::send_u64(fds[0], (uint64_t)N);
    CG_AHE::PublicKey fpk = CGNet::recv_pk(fds[0], cs);

    auto& pool = global_pool();
    size_t NT = pool.num_threads();

    // Receive all R1 ciphertext chunks before sending R2.  The reverse
    // firewalls are phase-separated (R1 forwarding loop, then R2 forwarding
    // loop), so sending R2 while R1 is still in flight can deadlock on socket
    // backpressure.  Chunking here limits compute granularity but preserves
    // the protocol phase boundary.
    std::cerr << "[" << role << "] [RECEIVE, ENCRYPT B, COMPUTE, SEND] Processing "
              << N << " OLEs across " << NT << " threads, chunk=" << CHUNK << "...\n";
    auto t_pre = std::chrono::high_resolution_clock::now();

    std::vector<CG_AHE::CipherText> pre_enc_b(N);
    std::vector<double> enc_b_ms(N, 0.0);
    std::cerr << "[" << role << "] [CIPHERTEXT RESPONSE STREAM] Starting pipeline stream...\n";
    auto t_online = std::chrono::high_resolution_clock::now();

    std::vector<CG_AHE::CipherText> ct_x(N), y_vals(N);
    std::vector<double> cmult_ms(N, 0.0), add_ms(N, 0.0);
    for (size_t begin = 0; begin < N; begin += CHUNK) {
        size_t end = std::min(N, begin + CHUNK);
        recv_ct_lanes_range(fds, ct_x, N, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            CG_AHE::ClearText b_ct(local_cg.cs(), b_vals[i]);
            auto op_t = Clock::now();
            pre_enc_b[i] = local_cg.encrypt(fpk, b_ct);
            enc_b_ms[i] = ms_since(op_t);
            op_t = Clock::now();
            CG_AHE::CipherText t = local_cg.cmult(ct_x[i], a_vals[i]);
            cmult_ms[i] = ms_since(op_t);
            op_t = Clock::now();
            y_vals[i] = local_cg.add(fpk, t, pre_enc_b[i]);
            add_ms[i] = ms_since(op_t);
        });
    }
    for (size_t begin = 0; begin < N; begin += CHUNK) {
        size_t end = std::min(N, begin + CHUNK);
        send_ct_lanes_range(fds, y_vals, N, begin, end);
    }
    std::cerr << "[" << role << "] " << N << "/" << N
              << " RF-OLE OLEs done over " << LANES << " lane(s)\n";

    double pre_ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t_pre).count();
    double online_ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t_online).count();
    g_last_rf_ole_online_ms = online_ms;
    std::cerr << "[" << role << "] receiving ciphertexts, encrypting B, computing, and sending responses done in " << pre_ms << " ms\n";
    std::cerr << "[" << role << "] ciphertext response stream done in " << online_ms << " ms\n";
    std::cerr << "[" << role << "] [OPTIME] encrypt_b sum=" << sum_ms(enc_b_ms)
              << " ms, avg=" << (N ? sum_ms(enc_b_ms) / (double)N : 0.0)
              << " ms/op\n";
    std::cerr << "[" << role << "] [OPTIME] cmult sum=" << sum_ms(cmult_ms)
              << " ms, avg=" << (N ? sum_ms(cmult_ms) / (double)N : 0.0)
              << " ms/op\n";
    std::cerr << "[" << role << "] [OPTIME] add sum=" << sum_ms(add_ms)
              << " ms, avg=" << (N ? sum_ms(add_ms) / (double)N : 0.0)
              << " ms/op\n";

    close_rf_lanes(fds);
}
