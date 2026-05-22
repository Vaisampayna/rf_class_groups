#include "cg_bench_io.hpp"
/*
 * Direct OPE sender.
 *
 * Loads or samples polynomial coefficients, converts Horner evaluation into
 * OLE sender pairs, runs direct batched OLE, then dumps coefficients.
 */
#include "cg_ope_common.hpp"
#include <iostream>
#include <sstream>

int main(int argc, char** argv)
{
    const char* receiver_ip = (argc > 1) ? argv[1] : LOCALHOST;
    size_t degree = (argc > 2) ? std::strtoull(argv[2], nullptr, 10) : 1000;
    const char* input_file = nullptr;
    const char* port = ope_port_rec();
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input-file" && i + 1 < argc) {
            input_file = argv[++i];
        } else {
            port = argv[i];
        }
    }
    if (degree == 0)
        throw std::runtime_error("degree must be at least 1");

    auto t_input = Clock::now();
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();
    std::vector<BICYCL::Mpz> coeffs = input_file
        ? read_mpz_coeff_file(input_file, q, "ope_sender")
        : random_poly_coeffs(degree, q);
    if (input_file && coeffs.size() != degree + 1) {
        std::ostringstream oss;
        oss << "ope_sender: expected degree+1=" << (degree + 1)
            << " coefficients in " << input_file << ", got " << coeffs.size();
        throw std::runtime_error(oss.str());
    }
    std::cerr << "[ope_sender] benchmark polynomial input loading excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    auto t_protocol = Clock::now();
    auto t_masks = Clock::now();
    std::vector<BICYCL::Mpz> a_vals, b_vals;
    build_ope_sender_ole_inputs(coeffs, q, a_vals, b_vals);
    std::cerr << "[ope_sender] OPE mask sampling and OLE pair build done in "
              << ms_since(t_masks) << " ms\n";
    std::cerr << "[ope_sender] built " << degree
              << " OLE sender pairs\n";

    auto t_total = Clock::now();
    direct_ole_batch_send(a_vals, b_vals, receiver_ip, port, "ope_sender", &cg);
    std::cerr << "[ope_sender] protocol/OLE exchange done in "
              << ms_since(t_total) << " ms\n";
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("ope_sender", protocol_ms);
    std::cerr << "[ope_sender] protocol end-to-end excluding input loading and offline dumps done in "
              << protocol_ms << " ms\n";

    auto t_dump = Clock::now();
    write_ope_sender_coeffs("direct_ope_sender_coeffs.txt", q, coeffs);
    std::cerr << "[ope_sender] sender polynomial written for offline checking in "
              << ms_since(t_dump) << " ms\n";

    std::cerr << "[ope_sender] total done in " << ms_since(t_total) << " ms\n";
    return 0;
}
