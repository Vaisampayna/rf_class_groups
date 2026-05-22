/**
 * cg_rf_psi2_reveal_rrf.cpp
 *
 * Receiver-side reverse firewall for two-way RF-PSI reveal-back.
 * Receives the sender-side mauled key, mauls once more for Party B, then
 * inverse-mauls/rerandomizes Party B's encrypted intersection on the way back.
 */
#include "cg_rf_ole_batch.hpp"
#include <iostream>

int main() {
    const char* port_rec = env_or_default("CG_PSI2_REVEAL_REC", "9043");
    const char* port_rfr = env_or_default("CG_PSI2_REVEAL_RFR", "9042");
    const size_t LANES = rf_transport_lanes();
    const size_t CHUNK = rf_chunk_size();

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();
    auto& pool = global_pool();
    prewarm_batch_pool();

    std::vector<int> rec_fds = connect_rf_lanes(LOCALHOST, port_rec, LANES);
    int lfd = CGNet::listen_tcp(port_rfr);
    std::cerr << "[psi2_reveal_rrf] listening on :" << port_rfr
              << " for " << LANES << " reveal lane(s)\n";
    std::vector<int> srf_fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    CG_AHE::PublicKey pk_prime = CGNet::recv_pk(srf_fds[0], cs);
    BICYCL::Mpz rho = rng.random_mpz(cs.secretkey_bound());
    CG_AHE::PublicKey pk_double = maul_pk(pk_prime, rho, cs);
    CGNet::send_pk(rec_fds[0], pk_double);

    uint64_t count = CGNet::recv_u64(rec_fds[0]);
    CGNet::send_u64(srf_fds[0], count);

    std::vector<CG_AHE::CipherText> in_buf((size_t)count), out_buf((size_t)count);
    for (size_t begin = 0; begin < (size_t)count; begin += CHUNK) {
        size_t end = std::min((size_t)count, begin + CHUNK);
        recv_ct_lanes_range(rec_fds, in_buf, (size_t)count, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            BICYCL::Mpz ri = thread_secure_randgen().random_mpz(cs.secretkey_bound());
            EncZero pre;
            local_cg.cs().power_of_h(pre.R, ri);
            pk_prime.exponentiation(local_cg.cs(), pre.E, ri);
            out_buf[i] = maul_inv_rerand(in_buf[i], rho, pre, cs);
        });
        send_ct_lanes_range(srf_fds, out_buf, (size_t)count, begin, end);
    }

    close_rf_lanes(srf_fds);
    close_rf_lanes(rec_fds);
    std::cerr << "[psi2_reveal_rrf] forwarded " << count
              << " reveal ciphertext(s)\n";
    return 0;
}
