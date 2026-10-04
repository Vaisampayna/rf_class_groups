#include "cg_opa.hpp"
/*
 * Direct OPA receiver.
 *
 * Loads receiver polynomial p_B, evaluates it at public OPA points, receives
 * OLE outputs for p_A(alpha)+r_A(alpha)*p_B(alpha), and dumps check data.
 */
#include "cg_bench_io.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0]
                  << " <n_pts> <d> <receiver_poly_0> ... <receiver_poly_d> [port]\n";
        return 2;
    }

    size_t n_pts = (size_t)std::strtoull(argv[1], nullptr, 10);
    size_t d = (size_t)std::strtoull(argv[2], nullptr, 10);
    const size_t receiver_coeffs = d + 1;

    const int coeff_begin = 3;
    const int coeff_end = coeff_begin + (int)receiver_coeffs;
    const bool bench64 = (argc > coeff_begin && std::string(argv[coeff_begin]) == "--bench64");
    const bool file_input = (argc > coeff_begin && std::string(argv[coeff_begin]) == "--input-file");
    if (!bench64 && !file_input && argc < coeff_end) {
        std::cerr << "[opa_receiver] need exactly d+1=" << receiver_coeffs
                  << " coefficients\n";
        return 1;
    }

    std::vector<BICYCL::Mpz> coeffs;
    coeffs.reserve(receiver_coeffs);
    if (bench64) {
        if (argc < coeff_begin + 2) {
            std::cerr << "[opa_receiver] --bench64 needs a domain seed\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        uint64_t domain = std::strtoull(argv[coeff_begin + 1], nullptr, 0);
        coeffs = benchmark_input_vector(receiver_coeffs, domain, cg.cs().cleartext_bound());
    } else if (file_input) {
        if (argc < coeff_begin + 2) {
            std::cerr << "[opa_receiver] --input-file needs a path\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        coeffs = read_mpz_coeff_file(argv[coeff_begin + 1],
                                     cg.cs().cleartext_bound(),
                                     "opa_receiver");
    } else {
        for (int i = coeff_begin; i < coeff_end; ++i)
            coeffs.emplace_back(std::string(argv[i]).c_str());
    }

    if (coeffs.size() != receiver_coeffs) {
        std::cerr << "[opa_receiver] need exactly d+1=" << receiver_coeffs
                  << " coefficients\n";
        return 1;
    }

    const int port_idx = (bench64 || file_input) ? coeff_begin + 2 : coeff_end;
    const char* port = (argc > port_idx) ? argv[port_idx] : ope_port_rec();
    // Receiver-owned protocol clock: starts after p_B coefficients are parsed
    // and stops after all OPA output evaluations are available in memory.
    auto t_protocol = Clock::now();
    OpaResult result = opa_receive(n_pts, coeffs, "opa_receiver", port);
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("opa_receiver", protocol_ms);
    std::cerr << "[opa_receiver] protocol end-to-end excluding input parsing and offline dumps done in "
              << protocol_ms << " ms\n";
    write_opa_receiver_dump("direct_opa_receiver_output.txt",
                            result.q, coeffs, result.alpha, result.y_vals);

    const char* print_results = std::getenv("CG_PRINT_RESULTS");
    if (print_results && std::string(print_results) == "1") {
        std::cout << "[opa_receiver] direct OPA evaluation results (alpha_i, y_i):\n";
        for (size_t i = 0; i < result.alpha.size(); ++i)
            std::cout << "  alpha=" << result.alpha[i]
                      << "  y=" << result.y_vals[i] << "\n";
    } else {
        std::cout << "[opa_receiver] direct OPA produced "
                  << result.y_vals.size()
                  << " evaluation value(s); set CG_PRINT_RESULTS=1 to print all values.\n";
    }
    return 0;
}
