#include "cg_ole3_common.hpp"
/*
 * Sender-side firewall for RF-OLE3.
 *
 * Implements the sender-side firewall from the RF-OLE3 protocol: adds its
 * a-blind under encryption in Round 1, key-mauls/rerandomizes ciphertext
 * traffic, and adds its b-blind to the plaintext z values in Round 3.
 */

#include <iostream>

int main(int argc, char** argv)
{
    const size_t LANES = rf_transport_lanes();
    const char* rfr_ip = (argc > 1) ? argv[1] : LOCALHOST;
    const char* port_rfr = rf_port_rfr();
    const char* port_rfs = rf_port_rfs();

    prewarm_batch_pool();

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();

    std::vector<int> rfr_fds = connect_rf_lanes(rfr_ip, port_rfr, LANES);

    int lfd = CGNet::listen_tcp(port_rfs);
    std::cerr << "[rf_sender_ole3] listening on :" << port_rfs
              << " for " << LANES << " lane(s)\n";
    std::vector<int> sender_fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    auto t_protocol = Clock::now();
    uint64_t n = CGNet::recv_u64(sender_fds[0]);
    CGNet::send_u64(rfr_fds[0], n);
    std::cerr << "[rf_sender_ole3] n_oles=" << n << "\n";

    CG_AHE::PublicKey pk = CGNet::recv_pk(sender_fds[0], cs);
    BICYCL::Mpz rho = rng.random_mpz(cs.secretkey_bound());
    CG_AHE::PublicKey pk_prime = maul_pk(pk, rho, cs);
    CGNet::send_pk(rfr_fds[0], pk_prime);
    std::cerr << "[rf_sender_ole3] maul public key and forward done\n";

    // Paper Round 1: sanitize the encrypted a coefficient by adding Enc(a')
    // before AlignEnc/Rerand.  The b coefficient is only key-aligned here; the
    // b' sanitization happens as a plaintext shift in Round 3.
    std::vector<BICYCL::Mpz> a_blinds =
        sample_plain_blinds_ole3((size_t)n, cs.cleartext_bound());
    std::vector<BICYCL::Mpz> b_blinds =
        sample_plain_blinds_ole3((size_t)n, cs.cleartext_bound());
    transform_ct_fwd_with_additive_blind_batch(
        sender_fds, rfr_fds, (size_t)n, rho, pk, pk_prime, cs, a_blinds,
        "rf_sender_ole3", "round 1 Enc(a)");
    transform_ct_fwd_batch(sender_fds, rfr_fds, (size_t)n, rho, pk_prime, cs,
                           "rf_sender_ole3", "round 1 Enc(b)");
    transform_ct_inv_batch(rfr_fds, sender_fds, (size_t)n, rho, pk, cs,
                           "rf_sender_ole3", "round 2 masked Enc(y)");

    auto t_z = Clock::now();
    std::vector<BICYCL::Mpz> z_vals = recv_mpz_lanes0(sender_fds);
    z_vals = add_plain_blind_mod_ole3(z_vals, b_blinds, cs.cleartext_bound());
    send_mpz_lanes0(rfr_fds, z_vals);
    std::cerr << "[rf_sender_ole3] round 3 plaintext z forward done in "
              << ms_since(t_z) << " ms\n";

    // Out-of-band artifact dump: the checker combines CS inputs with both
    // firewalls' blinds to verify the sanitized OLE relation.
    write_ole_blinds("rf_ole3_sender_firewall_blinds.txt", a_blinds, b_blinds);
    close_rf_lanes(sender_fds);
    close_rf_lanes(rfr_fds);
    std::cerr << "[rf_sender_ole3] protocol forwarding done in "
              << ms_since(t_protocol) << " ms\n";
    return 0;
}
