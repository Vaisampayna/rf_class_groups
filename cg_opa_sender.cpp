#include "cg_opa.hpp"
/*
 * Direct OPA sender.
 *
 * Loads the sender additive polynomial a(.), samples the sender masking
 * polynomial r(.), evaluates both at public OPA points, and runs direct
 * batched OLE so the receiver obtains a(alpha)+r(alpha)*x.
 */
#include "cg_bench_io.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 5) {
        std::cerr << "Usage: " << argv[0]
                  << " <receiver_ip> <n_pts> <d> <a_poly_0> ... <a_poly_2d> [port]\n";
        return 2;
    }

    const char* receiver_ip = argv[1];
    size_t n_pts = (size_t)std::strtoull(argv[2], nullptr, 10);
    size_t d = (size_t)std::strtoull(argv[3], nullptr, 10);
    const size_t additive_coeffs = 2 * d + 1;

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    std::vector<BICYCL::Mpz> additive_coeffs_A;
    int arg = 4;
    bool b_bench64 = (arg < argc && std::string(argv[arg]) == "--bench64");
    bool b_file = (arg < argc && std::string(argv[arg]) == "--input-file");
    if (b_bench64) {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --bench64 for b needs a domain seed\n";
            return 1;
        }
        uint64_t domain = std::strtoull(argv[arg + 1], nullptr, 0);
        additive_coeffs_A = benchmark_input_vector(additive_coeffs, domain, q);
        arg += 2;
    } else if (b_file) {
        if (argc < arg + 2) {
            std::cerr << "[opa_sender] --input-file for b needs a path\n";
            return 1;
        }
        additive_coeffs_A = read_mpz_coeff_file(argv[arg + 1], q, "opa_sender");
        arg += 2;
    } else {
        for (size_t i = 0; i < additive_coeffs && arg < argc; ++i, ++arg)
            additive_coeffs_A.emplace_back(std::string(argv[arg]).c_str());
    }

    if (additive_coeffs_A.size() != additive_coeffs) {
        std::cerr << "[opa_sender] need exactly 2d+1=" << additive_coeffs
                  << " additive coefficients; masking polynomial r is sampled internally\n";
        return 1;
    }

    const char* port = (argc > arg) ? argv[arg] : ope_port_rec();
    auto t_protocol = Clock::now();
    std::vector<BICYCL::Mpz> mask_coeffs_R = random_poly(d, q, rng);
    OpaSendEvals sent = opa_send_with_q(n_pts, additive_coeffs_A, mask_coeffs_R, q,
                                        receiver_ip, "opa_sender", port, &cg);
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("opa_sender", protocol_ms);
    std::cerr << "[opa_sender] protocol end-to-end excluding input parsing and offline dumps done in "
              << protocol_ms << " ms\n";
    write_opa_sender_coeffs("direct_opa_sender_coeffs.txt",
                            sent.q, additive_coeffs_A, mask_coeffs_R);
    std::cerr << "[opa_sender] done\n";
    return 0;
}
