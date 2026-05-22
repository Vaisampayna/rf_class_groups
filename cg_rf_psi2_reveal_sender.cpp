/**
 * cg_rf_psi2_reveal_sender.cpp
 *
 * Party A endpoint for the reveal-back phase of two-way RF-PSI.
 * After the ordinary one-way RF-PSI lets Party B learn the intersection,
 * Party A creates a fresh CG-AHE keypair and sends the public key through
 * S-RF and R-RF.  Party B encrypts each intersection element under the twice
 * mauled key, the firewalls inverse-maul on the way back, and Party A decrypts
 * under its original secret key.
 */
#include "cg_rf_ole_batch.hpp"
#include <iostream>

int main(int argc, char** argv) {
    std::string output_file = "psi2_sender_intersection.txt";
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--output-file")
            output_file = argv[i + 1];
    }

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();
    auto& pool = global_pool();
    prewarm_batch_pool();

    const size_t LANES = rf_transport_lanes();
    const char* port_rfs = env_or_default("CG_PSI2_REVEAL_RFS", "9041");

    auto t_protocol = Clock::now();
    std::vector<int> fds = connect_rf_lanes(LOCALHOST, port_rfs, LANES);

    CG_AHE::SecretKey sk = cg.keygen_sk();
    CG_AHE::PublicKey pk = cg.keygen_pk(sk);
    CGNet::send_pk(fds[0], pk);

    uint64_t count = CGNet::recv_u64(fds[0]);
    std::cerr << "[psi2_reveal_sender] expecting " << count
              << " encrypted intersection value(s)\n";

    std::vector<CG_AHE::CipherText> enc_values((size_t)count);
    recv_ct_lanes(fds, enc_values, (size_t)count);

    std::vector<BICYCL::Mpz> intersection((size_t)count);
    pool.parallel_for(0, (size_t)count, [&](size_t i) {
        CG_AHE::CG_Scheme& local_cg = worker_cg();
        CG_AHE::ClearText m = local_cg.decrypt(sk, enc_values[i]);
        intersection[i] = static_cast<const BICYCL::Mpz&>(m);
    });

    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("psi2_A", protocol_ms);
    write_mpz_list_file(output_file, intersection);
    close_rf_lanes(fds);

    std::cout << "[psi2_A] intersection:\n";
    if (intersection.empty()) {
        std::cout << "  (empty)\n";
    } else {
        for (size_t i = 0; i < intersection.size() && i < 20; ++i)
            std::cout << "  " << intersection[i] << "\n";
        if (intersection.size() > 20)
            std::cout << "  ... (showing first 20 only on stdout)\n";
    }
    std::cout << "[psi2_A] total intersection size: "
              << intersection.size() << "\n";
    std::cerr << "[psi2_reveal_sender] reveal-back phase done in "
              << protocol_ms << " ms\n";
    return 0;
}
