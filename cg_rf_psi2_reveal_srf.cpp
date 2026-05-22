/**
 * cg_rf_psi2_reveal_srf.cpp
 *
 * Sender-side reverse firewall for two-way RF-PSI reveal-back.
 * Direction 1: Party A pk -> S-RF maul -> R-RF.
 * Direction 2: ciphertexts from R-RF -> inverse-maul/rerand -> Party A.
 */
#include "cg_rf_ole_batch.hpp"
#include <iostream>

int main(int argc, char** argv) {
    const char* rrf_ip = (argc > 1) ? argv[1] : LOCALHOST;
    const char* port_rfs = env_or_default("CG_PSI2_REVEAL_RFS", "9041");
    const char* port_rfr = env_or_default("CG_PSI2_REVEAL_RFR", "9042");
    const size_t LANES = rf_transport_lanes();
    const size_t CHUNK = rf_chunk_size();

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();
    auto& pool = global_pool();
    prewarm_batch_pool();

    std::vector<int> rrf_fds = connect_rf_lanes(rrf_ip, port_rfr, LANES);
    int lfd = CGNet::listen_tcp(port_rfs);
    std::cerr << "[psi2_reveal_srf] listening on :" << port_rfs
              << " for " << LANES << " reveal lane(s)\n";
    std::vector<int> sender_fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    CG_AHE::PublicKey pk = CGNet::recv_pk(sender_fds[0], cs);
    BICYCL::Mpz rho = rng.random_mpz(cs.secretkey_bound());
    CG_AHE::PublicKey pk_prime = maul_pk(pk, rho, cs);
    CGNet::send_pk(rrf_fds[0], pk_prime);

    uint64_t count = CGNet::recv_u64(rrf_fds[0]);
    CGNet::send_u64(sender_fds[0], count);

    std::vector<CG_AHE::CipherText> in_buf((size_t)count), out_buf((size_t)count);
    for (size_t begin = 0; begin < (size_t)count; begin += CHUNK) {
        size_t end = std::min((size_t)count, begin + CHUNK);
        recv_ct_lanes_range(rrf_fds, in_buf, (size_t)count, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            BICYCL::Mpz ri = thread_secure_randgen().random_mpz(cs.secretkey_bound());
            EncZero pre;
            local_cg.cs().power_of_h(pre.R, ri);
            pk.exponentiation(local_cg.cs(), pre.E, ri);
            out_buf[i] = maul_inv_rerand(in_buf[i], rho, pre, cs);
        });
        send_ct_lanes_range(sender_fds, out_buf, (size_t)count, begin, end);
    }

    close_rf_lanes(sender_fds);
    close_rf_lanes(rrf_fds);
    std::cerr << "[psi2_reveal_srf] forwarded " << count
              << " reveal ciphertext(s)\n";
    return 0;
}
