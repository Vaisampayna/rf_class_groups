/**
 * cg_rf_psi2_reveal_receiver.cpp
 *
 * Party B endpoint for the reveal-back phase of two-way RF-PSI.
 * Reads the one-way PSI intersection already computed by Party B, receives
 * Party A's twice-mauled public key through the receiver-side firewall, and
 * encrypts every intersection element for the return path.
 */
#include "cg_rf_ole_batch.hpp"
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3 || std::string(argv[1]) != "--input-file") {
        std::cerr << "Usage: " << argv[0]
                  << " --input-file intersection_out.txt [--output-file copy.txt]\n";
        return 2;
    }

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();
    auto& pool = global_pool();
    prewarm_batch_pool();

    auto t_input = Clock::now();
    std::vector<BICYCL::Mpz> intersection;
    {
        std::ifstream in(argv[2]);
        if (!in)
            throw std::runtime_error(std::string("psi2_reveal_receiver: cannot open input file ") + argv[2]);
        std::string token;
        while (in >> token) {
            BICYCL::Mpz x(token.c_str());
            if (x.sgn() < 0 || x >= cs.cleartext_bound())
                throw std::runtime_error("psi2_reveal_receiver: input value outside plaintext field");
            intersection.push_back(x);
        }
    }
    std::cerr << "[psi2_reveal_receiver] read " << intersection.size()
              << " intersection value(s) in " << ms_since(t_input) << " ms\n";

    const size_t LANES = rf_transport_lanes();
    const char* port_rec = env_or_default("CG_PSI2_REVEAL_REC", "9043");

    auto t_protocol = Clock::now();
    int lfd = CGNet::listen_tcp(port_rec);
    std::cerr << "[psi2_reveal_receiver] listening on :" << port_rec
              << " for " << LANES << " reveal lane(s)\n";
    std::vector<int> fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    CG_AHE::PublicKey pk_double = CGNet::recv_pk(fds[0], cs);
    CGNet::send_u64(fds[0], (uint64_t)intersection.size());

    std::vector<CG_AHE::CipherText> enc_values(intersection.size());
    pool.parallel_for(0, intersection.size(), [&](size_t i) {
        CG_AHE::CG_Scheme& local_cg = worker_cg();
        CG_AHE::ClearText m(local_cg.cs(), intersection[i]);
        enc_values[i] = local_cg.encrypt(pk_double, m);
    });
    send_ct_lanes(fds, enc_values, intersection.size());

    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("psi2_B", protocol_ms);
    close_rf_lanes(fds);

    std::cout << "[psi2_B] intersection:\n";
    if (intersection.empty()) {
        std::cout << "  (empty)\n";
    } else {
        for (size_t i = 0; i < intersection.size() && i < 20; ++i)
            std::cout << "  " << intersection[i] << "\n";
        if (intersection.size() > 20)
            std::cout << "  ... (showing first 20 only on stdout)\n";
    }
    std::cout << "[psi2_B] total intersection size: "
              << intersection.size() << "\n";
    std::cerr << "[psi2_reveal_receiver] reveal-back phase done in "
              << protocol_ms << " ms\n";
    return 0;
}
