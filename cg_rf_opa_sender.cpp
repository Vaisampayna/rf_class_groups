/**
 * cg_rf_opa_sender.cpp — Sender for CG-AHE RF-OPA
 *
 * Oblivious Polynomial Addition:
 *   p_inter(X) = p_A(X) + r_A(X) * p_B(X)
 *   Evaluated at n_pts public points alpha_i.
 *   For each point i: OLE with (a_i = r_A(alpha_i), b_i = p_A(alpha_i), x_i = p_B(alpha_i))
 *   => y_i = a_i * x_i + b_i = p_inter(alpha_i)
 *
 * The sender samples r_A internally and drives all OLE instances over the
 * reverse-firewalled transport.
 *
 * Usage: ./cg_rf_opa_sender <n_pts> <d> <p_A coeffs degree 2d>
 */
#include "cg_rf_opa.hpp"
/*
 * Reverse-firewalled OPA sender.
 *
 * Evaluates sender-side OPA polynomials and sends the resulting OLE vectors
 * through the sender firewall.  Offline dumps are written after timing stops.
 */
#include "cg_bench_io.hpp"
#include <sstream>
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <n_pts> <d> <pA_0> ... <pA_2d>\n";
        return 2;
    }

    size_t n_pts = (size_t)std::strtoull(argv[1], nullptr, 10);
    size_t d = (size_t)std::strtoull(argv[2], nullptr, 10);
    const size_t additive_coeffs = 2 * d + 1;
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    // Parse p_A coefficients from argv.  r_A is sampled freshly by the sender.
    std::vector<BICYCL::Mpz> pA_coeffs;
    int arg = 3;
    if (arg < argc && std::string(argv[arg]) == "--bench64") {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --bench64 for pA needs a domain seed\n";
            return 1;
        }
        uint64_t domain = std::strtoull(argv[arg + 1], nullptr, 0);
        pA_coeffs = benchmark_input_vector(additive_coeffs, domain, q);
        arg += 2;
    } else if (arg < argc && std::string(argv[arg]) == "--input-file") {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --input-file for pA needs a path\n";
            return 1;
        }
        pA_coeffs = read_mpz_coeff_file(argv[arg + 1], q, "opa_sender");
        arg += 2;
    } else {
        for (; arg < argc; ++arg)
            pA_coeffs.emplace_back(std::string(argv[arg]).c_str());
    }

    if (pA_coeffs.size() != additive_coeffs) {
        std::cerr << "[opa_sender] need exactly 2d+1=" << additive_coeffs
                  << " additive coefficients; masking polynomial r is sampled internally\n";
        return 1;
    }

    auto t_protocol = Clock::now();
    std::vector<BICYCL::Mpz> rA_coeffs = random_poly(d, q, rng);
    RFOpaSendEvals sent = rf_opa_send_with_q(n_pts, pA_coeffs, rA_coeffs,
                                             q, "opa_sender");
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("opa_sender", protocol_ms);
    std::cerr << "[opa_sender] protocol end-to-end excluding input parsing and offline dumps done in "
              << protocol_ms << " ms\n";
    write_opa_sender_coeffs("rf_opa_sender_coeffs.txt",
                            sent.q, pA_coeffs, rA_coeffs);
    std::cerr << "[opa_sender] done\n";
    return 0;
}
