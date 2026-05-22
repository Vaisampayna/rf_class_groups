#include "cg_opa.hpp"
/*
 * Direct two-way PSI sender.
 *
 * Party A first participates in the same one-way direct PSI protocol as
 * cg_psi_sender.  After Party B reconstructs the intersection, Party A listens
 * on a reveal socket and receives the intersection values in the clear.
 */

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <receiver_ip> <m_B> --input-file set_A.txt [--port P] [--reveal-port P] [--output-file out.txt]\n"
                  << "   or: " << argv[0] << " <receiver_ip> <m_B> <elem1> <elem2> ... [--port P] [--reveal-port P]\n"
                  << "   or: " << argv[0] << " <receiver_ip> <m_B> --random <m_A> [seed] [--port P] [--reveal-port P]\n";
        return 2;
    }

    const char* receiver_ip = argv[1];
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    size_t m_B = std::strtoull(argv[2], nullptr, 10);
    std::vector<BICYCL::Mpz> set_A;
    const char* port = ope_port_rec();
    const char* reveal_port = env_or_default("CG_PSI2_DIRECT_REVEAL_PORT", "9143");
    std::string output_file = "psi2_sender_intersection.txt";

    auto t_input = Clock::now();
    for (int i = 3; i + 1 < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port")
            port = argv[i + 1];
        else if (arg == "--reveal-port")
            reveal_port = argv[i + 1];
        else if (arg == "--output-file")
            output_file = argv[i + 1];
    }

    if (std::string(argv[3]) == "--input-file") {
        if (argc < 5) {
            std::cerr << "[psi2_sender] --input-file requires a path\n";
            return 2;
        }
        set_A = read_mpz_list_file(argv[4], q, "psi2_sender");
        std::cerr << "[psi2_sender] Read S_A from " << argv[4] << "\n";
    } else if (std::string(argv[3]) == "--random") {
        size_t m_A = (argc > 4) ? std::strtoull(argv[4], nullptr, 10) : 1000;
        uint64_t seed = (argc > 5 && std::string(argv[5]) != "--port"
                         && std::string(argv[5]) != "--reveal-port")
            ? std::strtoull(argv[5], nullptr, 10) : 42;
        std::vector<BICYCL::Mpz> ignored_B;
        make_random_psi_sets_full_field(m_A, m_B, seed, q, set_A, ignored_B);
        std::cerr << "[psi2_sender] Generated random S_A of size " << m_A
                  << " over Z_q with ~" << (std::min(m_A, m_B) / 5)
                  << " expected overlaps (benchmark seed=" << seed << ")\n";
    } else {
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "--port" || arg == "--reveal-port" || arg == "--output-file") && i + 1 < argc) {
                ++i;
                continue;
            }
            set_A.emplace_back(arg.c_str());
        }
    }
    std::cerr << "[psi2_sender] benchmark set input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    auto t_protocol = Clock::now();
    auto t_poly = Clock::now();
    size_t m_A = set_A.size();
    size_t n_pts = m_A + m_B + 1;
    std::cerr << "[psi2_sender] direct two-way PSI |S_A|=" << m_A
              << ", |S_B|=" << m_B << ", n_pts=" << n_pts << "\n";

    std::vector<BICYCL::Mpz> pA = poly_from_roots(set_A, q);
    std::vector<BICYCL::Mpz> rA = random_poly(m_A, q, rng);
    std::vector<BICYCL::Mpz> rA_prime = random_poly(m_B, q, rng);
    std::vector<BICYCL::Mpz> qA = poly_mul_mod(pA, rA_prime, q);
    std::cerr << "[psi2_sender] vanishing polynomial and random mask polynomials generation done in "
              << ms_since(t_poly) << " ms\n";

    auto t_exchange = Clock::now();
    opa_send_with_q(n_pts, qA, rA, q, receiver_ip, "psi2_sender", port);
    std::cerr << "[psi2_sender] direct OPA send-share exchange done in "
              << ms_since(t_exchange) << " ms\n";

    auto t_reveal = Clock::now();
    int lfd = CGNet::listen_tcp(reveal_port);
    std::cerr << "[psi2_sender] listening on :" << reveal_port
              << " for clear reveal-back intersection\n";
    int fd = CGNet::accept_one(lfd);
    close(lfd);
    uint64_t count = CGNet::recv_u64(fd);
    std::vector<BICYCL::Mpz> intersection((size_t)count);
    for (size_t i = 0; i < intersection.size(); ++i)
        intersection[i] = CGNet::recv_mpz(fd);
    close(fd);
    std::cerr << "[psi2_sender] clear reveal-back receive done in "
              << ms_since(t_reveal) << " ms\n";

    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("psi2_A", protocol_ms);
    write_mpz_list_file(output_file, intersection);

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
    std::cerr << "[psi2_sender] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
