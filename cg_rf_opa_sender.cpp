/**
 * cg_rf_opa_sender.cpp — Sender for CG-AHE RF-OPA
 *
 * Oblivious Polynomial Addition:
 *   p_inter(X) = p_A(X) + r_A(X) * p_B(X)
 *   Evaluated at 2m+1 points alpha_i = i+1.
 *   For each point i: OLE with (a_i = r_A(alpha_i), b_i = p_A(alpha_i), x_i = p_B(alpha_i))
 *   => y_i = a_i * x_i + b_i = p_inter(alpha_i)
 *
 * The Sender drives all 2m+1 OLE instances over a single persistent connection.
 * The 4 RF processes (rf_receiver, rf_receiver_firewall, rf_sender_firewall)
 * are reused unchanged — they simply loop over N_OLE iterations.
 *
 * Usage: ./cg_rf_opa_sender <m> <p_A coeffs space-sep> -- <r_A coeffs space-sep>
 * Example (m=2): ./cg_rf_opa_sender 2 1 2 3 -- 4 5 6
 *   p_A(X) = 3X^2+2X+1,  r_A(X) = 6X^2+5X+4
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
        std::cerr << "Usage: " << argv[0] << " <m_A> <m_B> <pA_0> ... <pA_m_A> -- <rA_0> ... <rA_m_A>\n";
        return 2;
    }

    size_t m_A = (size_t)std::atoi(argv[1]);
    size_t m_B = (size_t)std::atoi(argv[2]);
    size_t n_pts = m_A + m_B + 1;   // number of evaluation points

    // Parse p_A and r_A coefficients from argv
    std::vector<BICYCL::Mpz> pA_coeffs, rA_coeffs;
    int arg = 3;
    if (arg < argc && std::string(argv[arg]) == "--bench64") {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --bench64 for pA needs a domain seed\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        uint64_t domain = std::strtoull(argv[arg + 1], nullptr, 0);
        pA_coeffs = benchmark_input_vector(m_A + 1, domain, cg.cs().cleartext_bound());
        arg += 2;
    } else if (arg < argc && std::string(argv[arg]) == "--input-file") {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --input-file for pA needs a path\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        pA_coeffs = read_mpz_coeff_file(argv[arg + 1],
                                        cg.cs().cleartext_bound(),
                                        "opa_sender");
        arg += 2;
    } else {
        for (; arg < argc && std::string(argv[arg]) != "--"; ++arg)
            pA_coeffs.emplace_back(std::string(argv[arg]).c_str());
    }
    ++arg; // skip "--"
    if (arg < argc && std::string(argv[arg]) == "--bench64") {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --bench64 for rA needs a domain seed\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        uint64_t domain = std::strtoull(argv[arg + 1], nullptr, 0);
        rA_coeffs = benchmark_input_vector(m_A + 1, domain, cg.cs().cleartext_bound());
    } else if (arg < argc && std::string(argv[arg]) == "--input-file") {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --input-file for rA needs a path\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        rA_coeffs = read_mpz_coeff_file(argv[arg + 1],
                                        cg.cs().cleartext_bound(),
                                        "opa_sender");
    } else {
        for (; arg < argc; ++arg)
            rA_coeffs.emplace_back(std::string(argv[arg]).c_str());
    }

    if (pA_coeffs.size() != m_A+1 || rA_coeffs.size() != m_A+1) {
        std::cerr << "[opa_sender] need exactly m_A+1=" << m_A+1 << " coefficients each\n";
        return 1;
    }

    RFOpaSendEvals sent = rf_opa_send(n_pts, pA_coeffs, rA_coeffs, "opa_sender");
    const double protocol_ms = sent.protocol_ms;
    write_protocol_timing_file_from_env("opa_sender", protocol_ms);
    std::cerr << "[opa_sender] protocol end-to-end excluding input parsing and offline dumps done in "
              << protocol_ms << " ms\n";
    write_opa_sender_coeffs("rf_opa_sender_coeffs.txt",
                            sent.q, pA_coeffs, rA_coeffs);
    std::cerr << "[opa_sender] done\n";
    return 0;
}
