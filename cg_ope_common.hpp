#pragma once
/*
 * Shared OPE and direct-OLE utilities.
 *
 * Implements polynomial evaluation, the Horner-to-OLE reduction for OPE, and
 * the chunked direct batched OLE transport used by OPE/OPA/PSI.
 */

#include "cg_rf_ole_batch.hpp"
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

static constexpr const char* OPE_PORT_REC = "9103";
static constexpr const char* OPE_PORT_VERIFY = "9104";

inline const char* ope_port_rec()
{
    return env_or_default("CG_OPE_PORT_REC", OPE_PORT_REC);
}

inline const char* ope_port_verify()
{
    return env_or_default("CG_OPE_PORT_VERIFY", OPE_PORT_VERIFY);
}

inline void mod_q(BICYCL::Mpz& x, const BICYCL::Mpz& q)
{
    BICYCL::Mpz::mod(x, x, q);
}

inline BICYCL::Mpz add_mod(const BICYCL::Mpz& a, const BICYCL::Mpz& b, const BICYCL::Mpz& q)
{
    BICYCL::Mpz out;
    BICYCL::Mpz::add(out, a, b);
    mod_q(out, q);
    return out;
}

inline BICYCL::Mpz sub_mod(const BICYCL::Mpz& a, const BICYCL::Mpz& b, const BICYCL::Mpz& q)
{
    BICYCL::Mpz out;
    BICYCL::Mpz::sub(out, a, b);
    mod_q(out, q);
    return out;
}

inline BICYCL::Mpz mul_mod(const BICYCL::Mpz& a, const BICYCL::Mpz& b, const BICYCL::Mpz& q)
{
    BICYCL::Mpz out;
    BICYCL::Mpz::mul(out, a, b);
    mod_q(out, q);
    return out;
}

inline std::vector<BICYCL::Mpz> random_poly_coeffs(size_t degree, const BICYCL::Mpz& q)
{
    // These are benchmark input coefficients, not protocol masks.  The input
    // domain can be narrowed with CG_BENCH_INPUT_BITS while OPE masks below
    // remain full-field random values.
    thread_local BICYCL::RandGen rng = make_secure_randgen();
    BICYCL::Mpz bound = benchmark_input_bound(q);
    std::vector<BICYCL::Mpz> coeffs(degree + 1);
    for (auto& c : coeffs)
        c = rng.random_mpz(bound);
    return coeffs;
}

inline BICYCL::Mpz eval_poly_horner(
    const std::vector<BICYCL::Mpz>& coeffs,
    const BICYCL::Mpz& alpha,
    const BICYCL::Mpz& q)
{
    // Standard Horner evaluation used for direct correctness checks and the
    // final local OPE reconstruction.
    if (coeffs.empty())
        return BICYCL::Mpz(0UL);
    BICYCL::Mpz acc = coeffs.back();
    for (size_t idx = coeffs.size() - 1; idx-- > 0;) {
        BICYCL::Mpz::mul(acc, acc, alpha);
        BICYCL::Mpz::add(acc, acc, coeffs[idx]);
        mod_q(acc, q);
    }
    return acc;
}

inline void build_ope_sender_ole_inputs(
    const std::vector<BICYCL::Mpz>& coeffs,
    const BICYCL::Mpz& q,
    std::vector<BICYCL::Mpz>& a_vals,
    std::vector<BICYCL::Mpz>& b_vals)
{
    // Convert a degree-d OPE instance into d OLE instances.  The sender masks
    // each Horner state with rho_i so Bob only learns the final p(alpha).
    if (coeffs.size() < 2)
        throw std::runtime_error("OPE requires degree >= 1");

    const size_t d = coeffs.size() - 1;
    a_vals.assign(d, BICYCL::Mpz(0UL));
    b_vals.assign(d, BICYCL::Mpz(0UL));

    thread_local BICYCL::RandGen rng = make_secure_randgen();
    BICYCL::Mpz s_prev = coeffs[d];

    // Each OLE implements one masked Horner transition. A sends (A_i, B_i);
    // Bob receives A_i * alpha + B_i and folds it into his masked state.
    for (size_t i = 1; i < d; ++i) {
        BICYCL::Mpz rho = rng.random_mpz(q);
        a_vals[i - 1] = s_prev;
        b_vals[i - 1] = sub_mod(coeffs[d - i], rho, q);
        s_prev = rho;
    }

    a_vals[d - 1] = s_prev;
    b_vals[d - 1] = coeffs[0];
}

inline BICYCL::Mpz finish_ope_receiver(
    const std::vector<BICYCL::Mpz>& y_vals,
    const BICYCL::Mpz& alpha,
    const BICYCL::Mpz& q)
{
    // Bob's masked Horner recurrence.  y_i are the OLE outputs generated from
    // build_ope_sender_ole_inputs(), all evaluated at the same alpha.
    if (y_vals.empty())
        throw std::runtime_error("OPE requires at least one OLE output");

    BICYCL::Mpz s_b(0UL);
    for (size_t i = 0; i + 1 < y_vals.size(); ++i) {
        s_b = add_mod(y_vals[i], mul_mod(alpha, s_b, q), q);
    }
    return add_mod(y_vals.back(), mul_mod(alpha, s_b, q), q);
}

inline void send_mpz_vec_ope(int fd, const std::vector<BICYCL::Mpz>& v)
{
    CGNet::send_u64(fd, (uint64_t)v.size());
    for (const auto& x : v)
        CGNet::send_mpz(fd, x);
}

inline std::vector<BICYCL::Mpz> recv_mpz_vec_ope(int fd)
{
    uint64_t n = CGNet::recv_u64(fd);
    std::vector<BICYCL::Mpz> v((size_t)n);
    for (auto& x : v)
        x = CGNet::recv_mpz(fd);
    return v;
}

inline std::vector<BICYCL::Mpz> direct_ole_batch_receive(
    const std::vector<BICYCL::Mpz>& x_vals,
    const char* port,
    const std::string& role,
    CG_AHE::CG_Scheme* existing_cg = nullptr)
{
    // Direct OLE is lock-step chunked: receive one response chunk before
    // sending the next.  This is important for OPA/PSI where both directions
    // carry many ciphertexts and socket buffers can otherwise fill.
    const size_t N = x_vals.size();
    const size_t LANES = rf_transport_lanes();
    const size_t CHUNK = rf_chunk_size();
    prewarm_batch_pool();

    BICYCL::RandGen rng = make_secure_randgen();
    std::unique_ptr<CG_AHE::CG_Scheme> owned_cg;
    if (!existing_cg) {
        BICYCL::RandGen public_rng = make_public_param_randgen();
        owned_cg.reset(new CG_AHE::CG_Scheme(CG_Q_NBITS, CG_K, CG_SECLEVEL,
                                             public_rng, rng));
    }
    CG_AHE::CG_Scheme& cg = existing_cg ? *existing_cg : *owned_cg;
    CG_AHE::SecretKey sk = cg.keygen_sk();
    CG_AHE::PublicKey pk = cg.keygen_pk(sk);

    int lfd = CGNet::listen_tcp(port);
    std::cerr << "[" << role << "] listening on :" << port
              << " for " << LANES << " lane(s)\n";
    std::vector<int> fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    uint64_t n_rx = CGNet::recv_u64(fds[0]);
    if (n_rx != N)
        throw std::runtime_error("direct OLE receiver count mismatch");
    CGNet::send_pk(fds[0], pk);

    auto& pool = global_pool();
    std::vector<CG_AHE::CipherText> enc_x(N), enc_y(N);
    std::vector<double> enc_ms(N, 0.0), dec_ms(N, 0.0);
    std::cerr << "[" << role << "] direct chunked encrypt/send, chunk="
              << CHUNK << "\n";
    auto t_exchange = Clock::now();
    std::vector<BICYCL::Mpz> y_vals(N);
    for (size_t begin = 0; begin < N; begin += CHUNK) {
        const size_t end = std::min(N, begin + CHUNK);
        pool.parallel_for(begin, end, [&](size_t i) {
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            CG_AHE::ClearText x_ct(local_cg.cs(), x_vals[i]);
            auto op_t = Clock::now();
            enc_x[i] = local_cg.encrypt(pk, x_ct);
            enc_ms[i] = ms_since(op_t);
        });
        send_ct_lanes_range(fds, enc_x, N, begin, end);
        // The sender consumes one chunk and immediately returns that chunk's
        // response.  Reading here prevents full-duplex socket-buffer deadlock
        // for large OPA/PSI batches.
        recv_ct_lanes_range(fds, enc_y, N, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            auto op_t = Clock::now();
            CG_AHE::ClearText y_ct = local_cg.decrypt(sk, enc_y[i]);
            dec_ms[i] = ms_since(op_t);
            y_vals[i] = static_cast<const BICYCL::Mpz&>(y_ct);
        });
    }
    std::cerr << "[" << role << "] direct sending ciphertexts, receiving outputs, and decrypting done in "
              << ms_since(t_exchange) << " ms\n";
    std::cerr << "[" << role << "] [OPTIME] direct encrypt_x sum="
              << sum_ms(enc_ms) << " ms, avg="
              << (N ? sum_ms(enc_ms) / (double)N : 0.0) << " ms/op\n";
    std::cerr << "[" << role << "] [OPTIME] direct decrypt_y sum="
              << sum_ms(dec_ms) << " ms, avg="
              << (N ? sum_ms(dec_ms) / (double)N : 0.0) << " ms/op\n";

    close_rf_lanes(fds);
    return y_vals;
}

inline void direct_ole_batch_send(
    const std::vector<BICYCL::Mpz>& a_vals,
    const std::vector<BICYCL::Mpz>& b_vals,
    const char* host,
    const char* port,
    const std::string& role,
    CG_AHE::CG_Scheme* existing_cg = nullptr)
{
    // Sender half of direct batched OLE.  For each chunk it receives Enc(x_i),
    // computes Enc(a_i*x_i+b_i), and immediately returns that chunk.
    if (a_vals.size() != b_vals.size())
        throw std::runtime_error("direct OLE sender vector size mismatch");

    const size_t N = a_vals.size();
    const size_t LANES = rf_transport_lanes();
    const size_t CHUNK = rf_chunk_size();
    prewarm_batch_pool();

    BICYCL::RandGen rng = make_secure_randgen();
    std::unique_ptr<CG_AHE::CG_Scheme> owned_cg;
    if (!existing_cg) {
        BICYCL::RandGen public_rng = make_public_param_randgen();
        owned_cg.reset(new CG_AHE::CG_Scheme(CG_Q_NBITS, CG_K, CG_SECLEVEL,
                                             public_rng, rng));
    }
    CG_AHE::CG_Scheme& cg = existing_cg ? *existing_cg : *owned_cg;
    const auto& cs = cg.cs();

    std::vector<int> fds = connect_rf_lanes(host, port, LANES);
    std::cerr << "[" << role << "] connected to " << host << ":" << port
              << " with " << LANES << " lane(s)\n";

    CGNet::send_u64(fds[0], (uint64_t)N);
    CG_AHE::PublicKey pk = CGNet::recv_pk(fds[0], cs);

    auto& pool = global_pool();
    std::vector<CG_AHE::CipherText> enc_b(N), enc_x(N), enc_y(N);
    std::vector<double> enc_b_ms(N, 0.0), cmult_ms(N, 0.0), add_ms(N, 0.0);
    std::cerr << "[" << role << "] direct chunked receive/compute/send, chunk="
              << CHUNK << "\n";
    auto t_response_stream = Clock::now();
    for (size_t begin = 0; begin < N; begin += CHUNK) {
        const size_t end = std::min(N, begin + CHUNK);
        recv_ct_lanes_range(fds, enc_x, N, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            CG_AHE::ClearText b_ct(local_cg.cs(), b_vals[i]);
            auto op_t = Clock::now();
            enc_b[i] = local_cg.encrypt(pk, b_ct);
            enc_b_ms[i] = ms_since(op_t);
            op_t = Clock::now();
            CG_AHE::CipherText ax = local_cg.cmult(enc_x[i], a_vals[i]);
            cmult_ms[i] = ms_since(op_t);
            op_t = Clock::now();
            enc_y[i] = local_cg.add(pk, ax, enc_b[i]);
            add_ms[i] = ms_since(op_t);
        });
        send_ct_lanes_range(fds, enc_y, N, begin, end);
    }
    std::cerr << "[" << role << "] direct receiving inputs, encrypting B, computing, and sending outputs done in "
              << ms_since(t_response_stream) << " ms\n";
    std::cerr << "[" << role << "] [OPTIME] direct encrypt_b sum="
              << sum_ms(enc_b_ms) << " ms, avg="
              << (N ? sum_ms(enc_b_ms) / (double)N : 0.0) << " ms/op\n";
    std::cerr << "[" << role << "] [OPTIME] direct cmult sum="
              << sum_ms(cmult_ms) << " ms, avg="
              << (N ? sum_ms(cmult_ms) / (double)N : 0.0) << " ms/op\n";
    std::cerr << "[" << role << "] [OPTIME] direct add sum="
              << sum_ms(add_ms) << " ms, avg="
              << (N ? sum_ms(add_ms) / (double)N : 0.0) << " ms/op\n";

    close_rf_lanes(fds);
}
