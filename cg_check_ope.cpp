#include "cg_bench_io.hpp"
/*
 * Offline checker for OPE.
 *
 * Recomputes p(alpha) from the sender polynomial dump and compares it with
 * the receiver's protocol output.
 */
#include "cg_ope_common.hpp"

#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 3) {
        std::cerr << "usage: " << argv[0]
                  << " <sender_coeffs.txt> <receiver_output.txt>\n";
        return 2;
    }

    std::ifstream sin(argv[1]), rin(argv[2]);
    if (!sin || !rin) {
        std::cerr << "[check_ope] cannot open input files\n";
        return 2;
    }

    std::string magic_s, magic_r;
    size_t n_coeffs = 0;
    sin >> magic_s >> n_coeffs;
    rin >> magic_r;
    if (magic_s != "OPE_SENDER_V1" || magic_r != "OPE_RECEIVER_V1" || n_coeffs == 0) {
        std::cerr << "[check_ope] bad file format\n";
        return 2;
    }

    BICYCL::Mpz q_sender = read_mpz_text(sin);
    BICYCL::Mpz q_receiver = read_mpz_text(rin);
    BICYCL::Mpz alpha = read_mpz_text(rin);
    BICYCL::Mpz output = read_mpz_text(rin);
    if (q_sender != q_receiver) {
        std::cerr << "[check_ope] q mismatch between sender and receiver dumps\n";
        return 2;
    }

    std::vector<BICYCL::Mpz> coeffs(n_coeffs);
    for (auto& c : coeffs)
        c = read_mpz_text(sin);

    BICYCL::Mpz expected = eval_poly_horner(coeffs, alpha, q_sender);
    if (expected == output) {
        std::cout << "[check_ope] VERIFICATION SUCCESS: output=" << output << "\n";
        return 0;
    }

    std::cerr << "[check_ope] VERIFICATION FAILED: expected=" << expected
              << " actual=" << output << "\n";
    return 1;
}
