#include "cg_opa.hpp"
/*
 * Direct two-way PSI receiver.
 *
 * Party B runs the normal one-way direct PSI receiver, learns the intersection,
 * then sends that intersection directly back to Party A in the clear.
 */

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <sender_ip> <m_A> --input-file set_B.txt [--output-file intersection_out.txt] [--timing-file protocol_time.csv] [--port P] [--reveal-port P]\n"
                  << "   or: " << argv[0] << " <sender_ip> <m_A> <elem1> <elem2> ... [--port P] [--reveal-port P]\n"
                  << "   or: " << argv[0] << " <sender_ip> <m_A> --random <m_B> [seed] [--port P] [--reveal-port P]\n";
        return 2;
    }

    const char* sender_ip = argv[1];
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    size_t m_A = std::strtoull(argv[2], nullptr, 10);
    std::vector<BICYCL::Mpz> set_B;
    const char* port = ope_port_rec();
    const char* reveal_port = env_or_default("CG_PSI2_DIRECT_REVEAL_PORT", "9143");
    std::string output_file = "intersection_out.txt";
    std::string timing_file;

    auto t_input = Clock::now();
    for (int i = 3; i + 1 < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port")
            port = argv[i + 1];
        else if (arg == "--reveal-port")
            reveal_port = argv[i + 1];
        else if (arg == "--output-file")
            output_file = argv[i + 1];
        else if (arg == "--timing-file")
            timing_file = argv[i + 1];
    }

    if (std::string(argv[3]) == "--input-file") {
        if (argc < 5) {
            std::cerr << "[psi2_receiver] --input-file requires a path\n";
            return 2;
        }
        set_B = read_mpz_list_file(argv[4], q, "psi2_receiver");
        std::cerr << "[psi2_receiver] Read S_B from " << argv[4] << "\n";
    } else if (std::string(argv[3]) == "--random") {
        size_t m_B = (argc > 4) ? std::strtoull(argv[4], nullptr, 10) : 1000;
        uint64_t seed = (argc > 5 && std::string(argv[5]) != "--port"
                         && std::string(argv[5]) != "--reveal-port")
            ? std::strtoull(argv[5], nullptr, 10) : 99;
        std::vector<BICYCL::Mpz> ignored_A;
        make_random_psi_sets_full_field(m_A, m_B, seed, q, ignored_A, set_B);
        std::cerr << "[psi2_receiver] Generated random S_B of size " << m_B
                  << " over Z_q with ~" << (std::min(m_A, m_B) / 5)
                  << " expected overlaps (benchmark seed=" << seed << ")\n";
    } else {
        for (int i = 3; i < argc; ++i) {
            std::string arg = argv[i];
            if ((arg == "--port" || arg == "--reveal-port" ||
                 arg == "--output-file" || arg == "--timing-file") && i + 1 < argc) {
                ++i;
                continue;
            }
            set_B.emplace_back(arg.c_str());
        }
    }
    std::cerr << "[psi2_receiver] benchmark set input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    auto t_protocol = Clock::now();
    auto t_poly = Clock::now();
    size_t m_B = set_B.size();
    size_t n_pts = m_A + m_B + 1;
    std::cerr << "[psi2_receiver] direct two-way PSI |S_A|=" << m_A
              << ", |S_B|=" << m_B << ", n_pts=" << n_pts << "\n";

    std::vector<BICYCL::Mpz> pB = poly_from_roots(set_B, q);
    std::cerr << "[psi2_receiver] vanishing polynomial build done in "
              << ms_since(t_poly) << " ms\n";

    auto t_exchange = Clock::now();
    OpaResult opa = opa_receive_with_q(n_pts, pB, q, "psi2_receiver", port);
    std::cerr << "[psi2_receiver] direct OPA output-share receive done in "
              << ms_since(t_exchange) << " ms\n";

    auto t_output = Clock::now();
    std::vector<BICYCL::Mpz> eval_vals =
        lagrange_eval_batch_auto(opa.y_vals, set_B, opa.q);

    BICYCL::Mpz zero(0UL);
    std::vector<BICYCL::Mpz> intersection;
    for (size_t i = 0; i < set_B.size(); ++i) {
        if (eval_vals[i] == zero)
            intersection.push_back(set_B[i]);
    }
    std::cerr << "[psi2_receiver] output reconstruction and verification done in "
              << ms_since(t_output) << " ms\n";

    auto t_reveal = Clock::now();
    int fd = CGNet::connect_tcp_retry(sender_ip, reveal_port);
    CGNet::send_u64(fd, (uint64_t)intersection.size());
    for (const BICYCL::Mpz& x : intersection)
        CGNet::send_mpz(fd, x);
    close(fd);
    std::cerr << "[psi2_receiver] clear reveal-back send done in "
              << ms_since(t_reveal) << " ms\n";

    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file(timing_file, "psi2_B", protocol_ms);
    write_protocol_timing_file_from_env("psi2_B", protocol_ms);
    write_mpz_list_file(output_file, intersection);

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
    std::cerr << "[psi2_receiver] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
