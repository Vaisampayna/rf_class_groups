#pragma once
/*
 * Shared helpers for direct and reverse-firewalled three-round OLE.
 *
 * Contains lane-0 plaintext message helpers, ciphertext rerandomization
 * transforms used by OLE3 firewalls, and simple blind/vector utilities.
 */

#include "cg_bench_io.hpp"
#include "cg_rf_ole_batch.hpp"

#include <iostream>
#include <string>
#include <vector>

static constexpr const char* OLE3_PORT_REC = "9123";

inline const char* ole3_port_rec()
{
    return env_or_default("CG_OLE3_PORT_REC", OLE3_PORT_REC);
}

inline void send_mpz_lanes0(const std::vector<int>& fds,
                            const std::vector<BICYCL::Mpz>& values)
{
    // Round-3 plaintext z values are small compared with ciphertext traffic;
    // send them on lane 0 to keep the lane protocol simple.
    CGNet::send_u64(fds[0], (uint64_t)values.size());
    for (const auto& v : values)
        CGNet::send_mpz(fds[0], v);
}

inline std::vector<BICYCL::Mpz> recv_mpz_lanes0(const std::vector<int>& fds)
{
    uint64_t n = CGNet::recv_u64(fds[0]);
    std::vector<BICYCL::Mpz> values((size_t)n);
    for (auto& v : values)
        v = CGNet::recv_mpz(fds[0]);
    return values;
}

inline EncZero make_enc_zero_pair(const BICYCL::CL_HSMqk& cs,
                                  const CG_AHE::PublicKey& pk,
                                  BICYCL::RandGen& rng)
{
    // Explicit Enc(0) components used for rerandomization without constructing
    // a full CG_Scheme object in this helper.
    EncZero pre;
    BICYCL::Mpz s = rng.random_mpz(cs.secretkey_bound());
    cs.power_of_h(pre.R, s);
    pk.exponentiation(cs, pre.E, s);
    return pre;
}

inline CG_AHE::CipherText encrypt_plain_ole3(
    CG_AHE::CG_Scheme& cg,
    const CG_AHE::PublicKey& pk,
    const BICYCL::Mpz& value)
{
    CG_AHE::ClearText ct(cg.cs(), value);
    return cg.encrypt(pk, ct);
}

inline CG_AHE::CipherText add_ct_ole3(
    CG_AHE::CG_Scheme& cg,
    const CG_AHE::PublicKey& pk,
    const CG_AHE::CipherText& lhs,
    const CG_AHE::CipherText& rhs)
{
    return cg.add(pk, lhs, rhs);
}

inline void transform_ct_fwd_batch(
    const std::vector<int>& in_fds,
    const std::vector<int>& out_fds,
    size_t n,
    const BICYCL::Mpz& rho,
    const CG_AHE::PublicKey& target_pk,
    const BICYCL::CL_HSMqk& cs,
    const std::string& role,
    const std::string& label)
{
    // Firewall transform for ciphertexts moving from pk to pk*rho.  Each chunk
    // first prepares Enc(0) masks under the target key, then fuses mauling and
    // rerandomization before forwarding.
    const size_t CHUNK = rf_chunk_size();
    auto& pool = global_pool();
    std::vector<CG_AHE::CipherText> in(n), out(n);
    std::vector<EncZero> pre(n);
    auto t = Clock::now();
    for (size_t begin = 0; begin < n; begin += CHUNK) {
        size_t end = std::min(n, begin + CHUNK);
        pool.parallel_for(begin, end, [&](size_t i) {
            thread_local BICYCL::RandGen rng = make_secure_randgen();
            pre[i] = make_enc_zero_pair(cs, target_pk, rng);
        });
        recv_ct_lanes_range(in_fds, in, n, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            out[i] = maul_fwd_rerand(in[i], rho, pre[i], cs);
        });
        send_ct_lanes_range(out_fds, out, n, begin, end);
    }
    std::cerr << "[" << role << "] " << label
              << " forward-maul/rerand stream done in " << ms_since(t) << " ms\n";
}

inline void transform_ct_fwd_with_additive_blind_batch(
    const std::vector<int>& in_fds,
    const std::vector<int>& out_fds,
    size_t n,
    const BICYCL::Mpz& rho,
    const CG_AHE::PublicKey& source_pk,
    const CG_AHE::PublicKey& target_pk,
    const BICYCL::CL_HSMqk& cs,
    const std::vector<BICYCL::Mpz>& blinds,
    const std::string& role,
    const std::string& label)
{
    // Round-1 a-coefficient sanitation:
    //   Enc_pk(a) -> Enc_pk(a + a_blind) -> AlignEnc/Rerand under target_pk.
    // The b-ciphertext path does not use this helper because b is blinded
    // later as a plaintext output shift in Round 3.
    const size_t CHUNK = rf_chunk_size();
    auto& pool = global_pool();
    std::vector<CG_AHE::CipherText> in(n), added(n), out(n);
    std::vector<EncZero> pre(n);
    auto t = Clock::now();
    for (size_t begin = 0; begin < n; begin += CHUNK) {
        size_t end = std::min(n, begin + CHUNK);
        recv_ct_lanes_range(in_fds, in, n, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            thread_local BICYCL::RandGen rng = make_secure_randgen();
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            CG_AHE::CipherText enc_blind =
                encrypt_plain_ole3(local_cg, source_pk, blinds[i]);
            added[i] = add_ct_ole3(local_cg, source_pk, in[i], enc_blind);
            pre[i] = make_enc_zero_pair(cs, target_pk, rng);
        });
        pool.parallel_for(begin, end, [&](size_t i) {
            out[i] = maul_fwd_rerand(added[i], rho, pre[i], cs);
        });
        send_ct_lanes_range(out_fds, out, n, begin, end);
    }
    std::cerr << "[" << role << "] " << label
              << " add-a-blind/forward-maul/rerand stream done in "
              << ms_since(t) << " ms\n";
}

inline void transform_ct_inv_batch(
    const std::vector<int>& in_fds,
    const std::vector<int>& out_fds,
    size_t n,
    const BICYCL::Mpz& rho,
    const CG_AHE::PublicKey& target_pk,
    const BICYCL::CL_HSMqk& cs,
    const std::string& role,
    const std::string& label)
{
    // Inverse firewall transform for ciphertexts moving back from pk*rho to
    // pk, again rerandomizing every ciphertext before it leaves the firewall.
    const size_t CHUNK = rf_chunk_size();
    auto& pool = global_pool();
    std::vector<CG_AHE::CipherText> in(n), out(n);
    std::vector<EncZero> pre(n);
    auto t = Clock::now();
    for (size_t begin = 0; begin < n; begin += CHUNK) {
        size_t end = std::min(n, begin + CHUNK);
        pool.parallel_for(begin, end, [&](size_t i) {
            thread_local BICYCL::RandGen rng = make_secure_randgen();
            pre[i] = make_enc_zero_pair(cs, target_pk, rng);
        });
        recv_ct_lanes_range(in_fds, in, n, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            out[i] = maul_inv_rerand(in[i], rho, pre[i], cs);
        });
        send_ct_lanes_range(out_fds, out, n, begin, end);
    }
    std::cerr << "[" << role << "] " << label
              << " inverse-maul/rerand stream done in " << ms_since(t) << " ms\n";
}

inline std::vector<BICYCL::Mpz> sample_plain_blinds_ole3(size_t n, const BICYCL::Mpz& q)
{
    std::vector<BICYCL::Mpz> blinds(n);
    auto& pool = global_pool();
    pool.parallel_for(0, n, [&](size_t i) {
        thread_local BICYCL::RandGen rng = make_secure_randgen();
        blinds[i] = rng.random_mpz(q);
    });
    return blinds;
}

inline std::vector<BICYCL::Mpz> add_plain_blind_mod_ole3(
    const std::vector<BICYCL::Mpz>& values,
    const std::vector<BICYCL::Mpz>& blinds,
    const BICYCL::Mpz& q)
{
    if (values.size() != blinds.size())
        throw std::runtime_error("OLE3 plaintext blind size mismatch");
    std::vector<BICYCL::Mpz> out(values.size());
    auto& pool = global_pool();
    pool.parallel_for(0, values.size(), [&](size_t i) {
        BICYCL::Mpz::add(out[i], values[i], blinds[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    });
    return out;
}
