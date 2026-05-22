#include "cg_bench_io.hpp"
/*
 * Offline checker for OPA.
 *
 * Verifies every public-point output y(alpha) against
 * b(alpha)+a(alpha)*p_B(alpha) modulo q.
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
        std::cerr << "[check_opa] cannot open input files\n";
        return 2;
    }

    std::string magic_s, magic_r;
    size_t sender_degree_plus_1 = 0, receiver_degree_plus_1 = 0, n_pts = 0;
    sin >> magic_s >> sender_degree_plus_1;
    rin >> magic_r >> receiver_degree_plus_1 >> n_pts;
    if (magic_s != "OPA_SENDER_V1" || magic_r != "OPA_RECEIVER_V1" ||
        sender_degree_plus_1 == 0 || receiver_degree_plus_1 == 0 || n_pts == 0) {
        std::cerr << "[check_opa] bad file format\n";
        return 2;
    }

    BICYCL::Mpz q_sender = read_mpz_text(sin);
    BICYCL::Mpz q_receiver = read_mpz_text(rin);
    if (q_sender != q_receiver) {
        std::cerr << "[check_opa] q mismatch between sender and receiver dumps\n";
        return 2;
    }
    const BICYCL::Mpz& q = q_sender;

    std::vector<BICYCL::Mpz> b_coeffs(sender_degree_plus_1);
    std::vector<BICYCL::Mpz> a_coeffs(sender_degree_plus_1);
    std::vector<BICYCL::Mpz> pB_coeffs(receiver_degree_plus_1);

    for (auto& c : b_coeffs)
        c = read_mpz_text(sin);
    std::string sep;
    sin >> sep;
    if (sep != "--") {
        std::cerr << "[check_opa] missing sender separator\n";
        return 2;
    }
    for (auto& c : a_coeffs)
        c = read_mpz_text(sin);

    for (auto& c : pB_coeffs)
        c = read_mpz_text(rin);
    rin >> sep;
    if (sep != "--") {
        std::cerr << "[check_opa] missing receiver separator\n";
        return 2;
    }

    size_t errors = 0;
    for (size_t i = 0; i < n_pts; ++i) {
        BICYCL::Mpz alpha = read_mpz_text(rin);
        BICYCL::Mpz actual = read_mpz_text(rin);
        BICYCL::Mpz b = eval_poly_horner(b_coeffs, alpha, q);
        BICYCL::Mpz a = eval_poly_horner(a_coeffs, alpha, q);
        BICYCL::Mpz x = eval_poly_horner(pB_coeffs, alpha, q);
        BICYCL::Mpz expected;
        BICYCL::Mpz::mul(expected, a, x);
        BICYCL::Mpz::add(expected, expected, b);
        BICYCL::Mpz::mod(expected, expected, q);
        if (expected != actual) {
            if (errors < 5)
                std::cerr << "[check_opa] mismatch at alpha=" << alpha
                          << ": expected=" << expected << " actual=" << actual << "\n";
            ++errors;
        }
    }

    if (errors == 0) {
        std::cout << "[check_opa] VERIFICATION SUCCESS: " << n_pts
                  << " OPA evaluation outputs correct\n";
        return 0;
    }
    std::cerr << "[check_opa] VERIFICATION FAILED: " << errors
              << " / " << n_pts << " mismatches\n";
    return 1;
}
