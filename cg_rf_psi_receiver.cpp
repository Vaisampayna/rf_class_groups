/**
 * cg_rf_psi_receiver.cpp — Receiver for CG-AHE RF-PSI
 *
 * Preferred mode:
 *   ./cg_rf_psi_receiver <m_A> --input-file set_B.txt
 * Optional benchmark modes are kept for quick local experiments.
 */
#include "cg_rf_opa.hpp"
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <unordered_map>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <m_A> --input-file set_B.txt [--output-file intersection_out.txt] [--timing-file protocol_time.csv]\n"
                  << "   or: " << argv[0] << " <m_A> <elem1> <elem2> ...\n"
                  << "   or: " << argv[0] << " <m_A> --random <m_B> [seed]\n";
        return 2;
    }

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();
    const BICYCL::Mpz& q = cs.cleartext_bound();

    size_t m_A = std::stoull(argv[1]);
    std::vector<BICYCL::Mpz> set_B;
    std::string output_file = "intersection_out.txt";
    std::string timing_file;
    for (int i = 2; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--output-file")
            output_file = argv[i + 1];
        else if (std::string(argv[i]) == "--timing-file")
            timing_file = argv[i + 1];
    }

    auto t_input = Clock::now();
    if (std::string(argv[2]) == "--input-file") {
        if (argc < 4) {
            std::cerr << "[psi_receiver] --input-file requires a path\n";
            return 2;
        }
        set_B = read_mpz_list_file(argv[3], q, "psi_receiver");
        std::cerr << "[psi_receiver] Read S_B from " << argv[3] << "\n";
    } else if (std::string(argv[2]) == "--random") {
        size_t m_B = (argc > 3) ? std::stoull(argv[3]) : 1000;
        uint64_t seed = (argc > 4) ? std::stoull(argv[4]) : 99;
        std::vector<BICYCL::Mpz> ignored_A;
        make_random_psi_sets_full_field(m_A, m_B, seed, q, ignored_A, set_B);

        std::cerr << "[psi_receiver] Generated random S_B of size " << m_B
                  << " over Z_q with ~" << (std::min(m_A, m_B) / 5)
                  << " expected overlaps (benchmark seed=" << seed << ")\n";
    } else {
        for (int i = 2; i < argc; ++i) {
            if (std::string(argv[i]) == "--output-file" && i + 1 < argc) {
                ++i;
                continue;
            }
            set_B.emplace_back(std::string(argv[i]).c_str());
        }
    }
    std::cerr << "[psi_receiver] benchmark set input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    // Paper-facing RF-PSI clock: starts after set_B is loaded.  It includes
    // vanishing-polynomial construction, RF-OPA/RF-OLE traffic, interpolation,
    // and the membership test, but excludes writing the output/check files.
    auto t_poly = Clock::now();
    size_t m_B = set_B.size();
    size_t n_pts = m_A + m_B + 1;
    std::cerr << "[psi_receiver] |S_A|=" << m_A << ", |S_B|=" << m_B
              << ", n_pts=" << n_pts << "\n";

    std::vector<BICYCL::Mpz> pB = poly_from_roots(set_B, q);
    const double poly_ms = ms_since(t_poly);
    std::cerr << "[psi_receiver] vanishing polynomial build done in "
              << poly_ms << " ms\n";

    // The RF-OPA output is a value representation of the paper PSI polynomial
    // p_cap = q_A + r_A*p_B at public alpha_i points.  Intersection recovery
    // evaluates that value-defined polynomial at the receiver's own set
    // elements and checks for zeros.
    (void)cs;
    auto t_exchange = Clock::now();
    RFOpaResult opa = rf_opa_receive(n_pts, pB, "psi_receiver");
    std::cerr << "[psi_receiver] RF-OPA output-share receive done in "
              << ms_since(t_exchange) << " ms\n";

    auto t_output = Clock::now();
    std::cerr << "[psi_receiver] computing interpolation/evaluation for intersection test...\n";
    std::vector<BICYCL::Mpz> eval_vals = lagrange_eval_batch_auto(opa.y_vals, set_B, opa.q);

    BICYCL::Mpz zero(0UL);
    std::vector<BICYCL::Mpz> intersection;
    for (size_t i = 0; i < set_B.size(); ++i) {
        if (eval_vals[i] == zero)
            intersection.push_back(set_B[i]);
    }

    const double output_ms = ms_since(t_output);
    const double protocol_ms = poly_ms + opa.protocol_ms + output_ms;
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
            std::cout << "  ... (showing first 20 only on stdout)\n";
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
