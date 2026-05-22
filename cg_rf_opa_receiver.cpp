/**
 * cg_rf_opa_receiver.cpp — Receiver for CG-AHE RF-OPA
 *
 * Receives 2m+1 OLE outputs y_i = p_A(alpha_i) + r_A(alpha_i)*p_B(alpha_i)
 * These are the evaluations of p_inter at evaluation points.
 * Prints the evaluation points and values (for verification or further use).
 *
 * Usage: ./cg_rf_opa_receiver <m> <pB_0> ... <pB_m>
 * Example (m=2): ./cg_rf_opa_receiver 2 1 0 1
 *   p_B(X) = X^2 + 1
 */
#include "cg_rf_opa.hpp"
/*
 * Reverse-firewalled OPA receiver.
 *
 * Performs the receiver half of OPA over RF-OLE.  Public evaluation points and
 * receiver polynomial values are prepared before the timed RF-OLE exchange.
 */
#include "cg_bench_io.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <m_A> <m_B> <pB_0> ... <pB_m_B>\n";
        return 2;
    }

    size_t m_A = (size_t)std::atoi(argv[1]);
    size_t m_B = (size_t)std::atoi(argv[2]);
    size_t n_pts = m_A + m_B + 1;

    std::vector<BICYCL::Mpz> pB_coeffs;
    if (argc > 3 && std::string(argv[3]) == "--bench64") {
        if (argc < 5) {
            std::cerr << "[opa_receiver] --bench64 needs a domain seed\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        uint64_t domain = std::strtoull(argv[4], nullptr, 0);
        pB_coeffs = benchmark_input_vector(m_B + 1, domain, cg.cs().cleartext_bound());
    } else if (argc > 3 && std::string(argv[3]) == "--input-file") {
        if (argc < 5) {
            std::cerr << "[opa_receiver] --input-file needs a path\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        pB_coeffs = read_mpz_coeff_file(argv[4],
                                        cg.cs().cleartext_bound(),
                                        "opa_receiver");
    } else {
        for (int i = 3; i < argc; ++i)
            pB_coeffs.emplace_back(std::string(argv[i]).c_str());
    }

    if (pB_coeffs.size() != m_B+1) {
        std::cerr << "[opa_receiver] need exactly m_B+1=" << m_B+1 << " coefficients\n";
        return 1;
    }

    RFOpaResult result = rf_opa_receive(n_pts, pB_coeffs, "opa_receiver");
    const double protocol_ms = result.protocol_ms;
    write_protocol_timing_file_from_env("opa_receiver", protocol_ms);
    std::cerr << "[opa_receiver] protocol end-to-end excluding input parsing and offline dumps done in "
              << protocol_ms << " ms\n";
    write_opa_receiver_dump("rf_opa_receiver_output.txt",
                            result.q, pB_coeffs, result.alpha, result.y_vals);

    const char* print_results = std::getenv("CG_PRINT_RESULTS");
    if (print_results && std::string(print_results) == "1") {
        std::cout << "[opa_receiver] RF-OPA evaluation results (alpha_i, y_i):\n";
        for (size_t i = 0; i < result.alpha.size(); ++i)
            std::cout << "  alpha=" << result.alpha[i]
                      << "  y=" << result.y_vals[i] << "\n";
    } else {
        std::cout << "[opa_receiver] RF-OPA produced "
                  << result.y_vals.size()
                  << " evaluation value(s); set CG_PRINT_RESULTS=1 to print all values.\n";
    }
    return 0;
}
