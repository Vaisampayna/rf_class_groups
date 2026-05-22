#include "cg_opa.hpp"
/*
 * Direct one-sided PSI receiver.
 *
 * Builds the receiver set polynomial p_B, receives OPA shares over direct OLE,
 * evaluates the intersection-test polynomial on S_B, and outputs exactly the
 * receiver elements that are also in S_A.
 */

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <m_A> --input-file set_B.txt [--output-file intersection_out.txt] [--timing-file protocol_time.csv] [--port P]\n"
                  << "   or: " << argv[0] << " <m_A> <elem1> <elem2> ... [--port P]\n"
                  << "   or: " << argv[0] << " <m_A> --random <m_B> [seed] [--port P]\n";
        return 2;
    }

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    size_t m_A = std::strtoull(argv[1], nullptr, 10);
    std::vector<BICYCL::Mpz> set_B;
    const char* port = ope_port_rec();

    auto t_input = Clock::now();
    std::string output_file = "intersection_out.txt";
    std::string timing_file;
    for (int i = 2; i + 1 < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--port")
            port = argv[i + 1];
        else if (arg == "--output-file")
            output_file = argv[i + 1];
        else if (arg == "--timing-file")
            timing_file = argv[i + 1];
    }

    if (std::string(argv[2]) == "--input-file") {
        if (argc < 4) {
            std::cerr << "[psi_receiver] --input-file requires a path\n";
            return 2;
        }
        set_B = read_mpz_list_file(argv[3], q, "psi_receiver");
        std::cerr << "[psi_receiver] Read S_B from " << argv[3] << "\n";
    } else if (std::string(argv[2]) == "--random") {
        size_t m_B = (argc > 3) ? std::strtoull(argv[3], nullptr, 10) : 1000;
        uint64_t seed = (argc > 4 && std::string(argv[4]) != "--port")
            ? std::strtoull(argv[4], nullptr, 10) : 99;
        std::vector<BICYCL::Mpz> ignored_A;
        make_random_psi_sets_full_field(m_A, m_B, seed, q, ignored_A, set_B);
        std::cerr << "[psi_receiver] Generated random S_B of size " << m_B
                  << " over Z_q with ~" << (std::min(m_A, m_B) / 5)
                  << " expected overlaps (benchmark seed=" << seed << ")\n";
    } else {
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--port" && i + 1 < argc) {
                port = argv[i + 1];
                break;
            }
            if (std::string(argv[i]) == "--output-file" && i + 1 < argc) {
                ++i;
                continue;
            }
            if (std::string(argv[i]) == "--timing-file" && i + 1 < argc) {
                ++i;
                continue;
            }
            set_B.emplace_back(std::string(argv[i]).c_str());
        }
    }
    std::cerr << "[psi_receiver] benchmark set input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    // Protocol timing starts after input file parsing.  It includes polynomial
    // construction, OPA/OLE communication, interpolation, and the membership
    // test; it stops before writing result/check files.
    auto t_protocol = Clock::now();
    auto t_poly = Clock::now();
    size_t m_B = set_B.size();
    size_t n_pts = m_A + m_B + 1;
    std::cerr << "[psi_receiver] direct OLE PSI |S_A|=" << m_A
              << ", |S_B|=" << m_B << ", n_pts=" << n_pts << "\n";

    std::vector<BICYCL::Mpz> pB = poly_from_roots(set_B, q);
    std::cerr << "[psi_receiver] vanishing polynomial build done in "
              << ms_since(t_poly) << " ms\n";

    // OPA returns values of the paper PSI polynomial
    // p_cap = q_A + r_A*p_B at public alpha_i points.  The receiver does not
    // interpolate unless the auto policy selects it; for these sizes
    // barycentric evaluation on S_B is typically faster.
    auto t_exchange = Clock::now();
    OpaResult opa = opa_receive_with_q(n_pts, pB, q, "psi_receiver", port);
    std::cerr << "[psi_receiver] direct OPA output-share receive done in "
              << ms_since(t_exchange) << " ms\n";

    auto t_output = Clock::now();
    std::vector<BICYCL::Mpz> eval_vals =
        lagrange_eval_batch_auto(opa.y_vals, set_B, opa.q);

    // Membership test: for beta in S_B, p_B(beta)=0, so
    // p_cap(beta)=p_A(beta) r'_A(beta).  Thus beta is accepted when this value
    // is zero.  The only extra roots come from r'_A(beta)=0, which occur with
    // negligible probability over the 128-bit plaintext field.
    BICYCL::Mpz zero(0UL);
    std::vector<BICYCL::Mpz> intersection;
    for (size_t i = 0; i < set_B.size(); ++i) {
        if (eval_vals[i] == zero)
            intersection.push_back(set_B[i]);
    }
    const double output_ms = ms_since(t_output);
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file(timing_file, "psi_receiver", protocol_ms);
    write_protocol_timing_file_from_env("psi_receiver", protocol_ms);

    write_mpz_list_file(output_file, intersection);
    std::cerr << "[psi_receiver] wrote receiver intersection output to "
              << output_file << "\n";
    std::cout << "[psi_receiver] intersection S_A ∩ S_B:\n";
    if (intersection.empty()) {
        std::cout << "  (empty)\n";
    } else {
        for (size_t i = 0; i < intersection.size() && i < 20; ++i)
            std::cout << "  " << intersection[i] << "\n";
        if (intersection.size() > 20)
            std::cout << "  ... (showing first 20 only)\n";
        std::cout << "[psi_receiver] total intersection size: "
                  << intersection.size() << "\n";
    }
    std::cerr << "[psi_receiver] output reconstruction and verification done in "
              << output_ms << " ms\n";
    std::cerr << "[psi_receiver] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    if (!timing_file.empty())
        std::cerr << "[psi_receiver] wrote protocol timing to "
                  << timing_file << "\n";
    return 0;
}
