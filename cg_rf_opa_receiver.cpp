/**
 * cg_rf_opa_receiver.cpp — Receiver for CG-AHE RF-OPA
 *
 * Receives OPA outputs y_i = p_A(alpha_i) + r_A(alpha_i)*p_B(alpha_i)
 * These are the evaluations of p_inter at evaluation points.
 * Prints the evaluation points and values (for verification or further use).
 *
 * Usage: ./cg_rf_opa_receiver <n_pts> <d> <pB_0> ... <pB_d>
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
        std::cerr << "Usage: " << argv[0] << " <n_pts> <d> <pB_0> ... <pB_d>\n";
        return 2;
    }

    size_t n_pts = (size_t)std::strtoull(argv[1], nullptr, 10);
    size_t d = (size_t)std::strtoull(argv[2], nullptr, 10);
    const size_t receiver_coeffs = d + 1;

    std::vector<BICYCL::Mpz> pB_coeffs;
    if (argc > 3 && std::string(argv[3]) == "--bench64") {
        if (argc < 5) {
            std::cerr << "[opa_receiver] --bench64 needs a domain seed\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        uint64_t domain = std::strtoull(argv[4], nullptr, 0);
        pB_coeffs = benchmark_input_vector(receiver_coeffs, domain, cg.cs().cleartext_bound());
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

    if (pB_coeffs.size() != receiver_coeffs) {
        std::cerr << "[opa_receiver] need exactly d+1=" << receiver_coeffs << " coefficients\n";
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
