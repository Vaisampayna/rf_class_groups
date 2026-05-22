#include "cg_ole3_common.hpp"
/*
 * Receiver-side firewall for RF-OLE3.
 *
 * Implements the receiver-side firewall from the RF-OLE3 protocol: adds its
 * a-blind under encryption in Round 1, key-mauls/rerandomizes ciphertext
 * traffic, and adds its b-blind to plaintext z values in Round 3.
 */

#include <iostream>

int main()
{
    const size_t LANES = rf_transport_lanes();
    const char* port_rec = rf_port_rec();
    const char* port_rfr = rf_port_rfr();

    prewarm_batch_pool();

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();

    std::vector<int> rec_fds = connect_rf_lanes(LOCALHOST, port_rec, LANES);

    int lfd = CGNet::listen_tcp(port_rfr);
    std::cerr << "[rf_receiver_ole3] listening on :" << port_rfr
              << " for " << LANES << " lane(s)\n";
    std::vector<int> rfs_fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    auto t_protocol = Clock::now();
    uint64_t n = CGNet::recv_u64(rfs_fds[0]);
    CGNet::send_u64(rec_fds[0], n);
    std::cerr << "[rf_receiver_ole3] n_oles=" << n << "\n";

    CG_AHE::PublicKey pk = CGNet::recv_pk(rfs_fds[0], cs);
    BICYCL::Mpz rho = rng.random_mpz(cs.secretkey_bound());
    CG_AHE::PublicKey pk_prime = maul_pk(pk, rho, cs);
    CGNet::send_pk(rec_fds[0], pk_prime);
    std::cerr << "[rf_receiver_ole3] maul public key and forward done\n";

    // Paper Round 1: add the receiver-firewall a'' blind while the ciphertext is
    // still under the incoming key, then align/rerandomize to the outgoing key.
    std::vector<BICYCL::Mpz> a_blinds =
        sample_plain_blinds_ole3((size_t)n, cs.cleartext_bound());
    std::vector<BICYCL::Mpz> b_blinds =
        sample_plain_blinds_ole3((size_t)n, cs.cleartext_bound());
    transform_ct_fwd_with_additive_blind_batch(
        rfs_fds, rec_fds, (size_t)n, rho, pk, pk_prime, cs, a_blinds,
        "rf_receiver_ole3", "round 1 Enc(a)");
    transform_ct_fwd_batch(rfs_fds, rec_fds, (size_t)n, rho, pk_prime, cs,
                           "rf_receiver_ole3", "round 1 Enc(b)");
    transform_ct_inv_batch(rec_fds, rfs_fds, (size_t)n, rho, pk, cs,
                           "rf_receiver_ole3", "round 2 masked Enc(y)");

    auto t_z = Clock::now();
    std::vector<BICYCL::Mpz> z_vals = recv_mpz_lanes0(rfs_fds);
    z_vals = add_plain_blind_mod_ole3(z_vals, b_blinds, cs.cleartext_bound());
    send_mpz_lanes0(rec_fds, z_vals);
    std::cerr << "[rf_receiver_ole3] round 3 plaintext z forward done in "
              << ms_since(t_z) << " ms\n";

    write_ole_blinds("rf_ole3_receiver_firewall_blinds.txt", a_blinds, b_blinds);
    close_rf_lanes(rfs_fds);
    close_rf_lanes(rec_fds);
    std::cerr << "[rf_receiver_ole3] protocol forwarding done in "
              << ms_since(t_protocol) << " ms\n";
    return 0;
}
