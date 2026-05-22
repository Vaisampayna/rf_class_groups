#include "cg_opa.hpp"
/*
 * Direct OPA sender.
 *
 * Loads sender polynomials b(.) and a(.), evaluates them at public OPA points,
 * and runs direct batched OLE so the receiver obtains b(alpha)+a(alpha)*x.
 */
#include "cg_bench_io.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <receiver_ip> <m_A> <m_B> <b_poly_0> ... <b_poly_m_A>"
                  << " -- <a_poly_0> ... <a_poly_m_A> [port]\n";
        return 2;
    }

    const char* receiver_ip = argv[1];
    size_t m_A = (size_t)std::strtoull(argv[2], nullptr, 10);
    size_t m_B = (size_t)std::strtoull(argv[3], nullptr, 10);
    size_t n_pts = m_A + m_B + 1;

    std::vector<BICYCL::Mpz> b_coeffs, a_coeffs;
    int arg = 4;
    bool b_bench64 = (arg < argc && std::string(argv[arg]) == "--bench64");
    bool b_file = (arg < argc && std::string(argv[arg]) == "--input-file");
    if (b_bench64) {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --bench64 for b needs a domain seed\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        uint64_t domain = std::strtoull(argv[arg + 1], nullptr, 0);
        b_coeffs = benchmark_input_vector(m_A + 1, domain, cg.cs().cleartext_bound());
        arg += 2;
    } else if (b_file) {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --input-file for b needs a path\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        b_coeffs = read_mpz_coeff_file(argv[arg + 1],
                                       cg.cs().cleartext_bound(),
                                       "opa_sender");
        arg += 2;
    } else {
        for (; arg < argc && std::string(argv[arg]) != "--"; ++arg)
            b_coeffs.emplace_back(std::string(argv[arg]).c_str());
    }
    if (arg == argc) {
        std::cerr << "[opa_sender] missing -- separator\n";
        return 1;
    }
    ++arg;
    const int a_begin = arg;
    bool a_bench64 = (arg < argc && std::string(argv[arg]) == "--bench64");
    bool a_file = (arg < argc && std::string(argv[arg]) == "--input-file");
    int a_end = a_begin + (int)m_A + 1;
    if (a_bench64) {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --bench64 for a needs a domain seed\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        uint64_t domain = std::strtoull(argv[arg + 1], nullptr, 0);
        a_coeffs = benchmark_input_vector(m_A + 1, domain, cg.cs().cleartext_bound());
        a_end = arg + 2;
    } else if (a_file) {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --input-file for a needs a path\n";
            return 1;
        }
        BICYCL::RandGen rng = make_secure_randgen();
        CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
        a_coeffs = read_mpz_coeff_file(argv[arg + 1],
                                       cg.cs().cleartext_bound(),
                                       "opa_sender");
        a_end = arg + 2;
    } else {
        if (argc < a_end) {
            std::cerr << "[opa_sender] need exactly m_A+1=" << (m_A + 1)
                      << " coefficients after --\n";
            return 1;
        }
        for (; arg < a_end; ++arg)
            a_coeffs.emplace_back(std::string(argv[arg]).c_str());
    }

    if (b_coeffs.size() != m_A + 1 || a_coeffs.size() != m_A + 1) {
        std::cerr << "[opa_sender] need exactly m_A+1=" << (m_A + 1)
                  << " coefficients for each sender polynomial\n";
        return 1;
    }

    const char* port = (argc > a_end) ? argv[a_end] : ope_port_rec();
    auto t_protocol = Clock::now();
    OpaSendEvals sent = opa_send(n_pts, b_coeffs, a_coeffs, receiver_ip, "opa_sender", port);
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("opa_sender", protocol_ms);
    std::cerr << "[opa_sender] protocol end-to-end excluding input parsing and offline dumps done in "
              << protocol_ms << " ms\n";
    write_opa_sender_coeffs("direct_opa_sender_coeffs.txt",
                            sent.q, b_coeffs, a_coeffs);
    std::cerr << "[opa_sender] done\n";
    return 0;
}
