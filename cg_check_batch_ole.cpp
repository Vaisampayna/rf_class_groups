#include "cg_bench_io.hpp"
/*
 * Offline checker for OLE-like protocols.
 *
 * Reads sender (a_i,b_i) and receiver (x_i,y_i) dumps and verifies
 * y_i = a_i*x_i + b_i mod q for every instance.  If firewall blind dumps are
 * supplied, it first sanitizes the sender tuple as
 * (a_i + a'_i + a''_i, b_i + b'_i + b''_i).
 */

#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc != 3 && argc != 5) {
        std::cerr << "usage: " << argv[0]
                  << " <sender_inputs.txt> <receiver_io.txt> "
                  << "[sender_firewall_blinds.txt receiver_firewall_blinds.txt]\n";
        return 2;
    }

    std::ifstream sin(argv[1]), rin(argv[2]);
    if (!sin || !rin) {
        std::cerr << "[check_batch_ole] cannot open input files\n";
        return 2;
    }

    std::string magic_s, magic_r;
    size_t ns = 0, nr = 0;
    sin >> magic_s >> ns;
    rin >> magic_r >> nr;
    if (magic_s != "OLE_SENDER_V1" || magic_r != "OLE_RECEIVER_V1" || ns != nr) {
        std::cerr << "[check_batch_ole] bad file format or size mismatch\n";
        return 2;
    }

    BICYCL::Mpz q = read_mpz_text(rin);
    std::vector<BICYCL::Mpz> a_blind_sum(ns, BICYCL::Mpz(0UL));
    std::vector<BICYCL::Mpz> b_blind_sum(ns, BICYCL::Mpz(0UL));
    if (argc == 5) {
        for (int file_idx = 3; file_idx <= 4; ++file_idx) {
            std::ifstream bin(argv[file_idx]);
            if (!bin) {
                std::cerr << "[check_batch_ole] cannot open blind file "
                          << argv[file_idx] << "\n";
                return 2;
            }
            std::string magic_b;
            size_t nb = 0;
            bin >> magic_b >> nb;
            if (magic_b != "OLE_BLINDS_V1" || nb != ns) {
                std::cerr << "[check_batch_ole] bad blind file format or size mismatch: "
                          << argv[file_idx] << "\n";
                return 2;
            }
            for (size_t i = 0; i < ns; ++i) {
                BICYCL::Mpz a_blind = read_mpz_text(bin);
                BICYCL::Mpz b_blind = read_mpz_text(bin);
                BICYCL::Mpz::add(a_blind_sum[i], a_blind_sum[i], a_blind);
                BICYCL::Mpz::mod(a_blind_sum[i], a_blind_sum[i], q);
                BICYCL::Mpz::add(b_blind_sum[i], b_blind_sum[i], b_blind);
                BICYCL::Mpz::mod(b_blind_sum[i], b_blind_sum[i], q);
            }
        }
    }

    size_t errors = 0;
    for (size_t i = 0; i < ns; ++i) {
        BICYCL::Mpz a = read_mpz_text(sin);
        BICYCL::Mpz b = read_mpz_text(sin);
        BICYCL::Mpz x = read_mpz_text(rin);
        BICYCL::Mpz y = read_mpz_text(rin);

        BICYCL::Mpz::add(a, a, a_blind_sum[i]);
        BICYCL::Mpz::mod(a, a, q);
        BICYCL::Mpz::add(b, b, b_blind_sum[i]);
        BICYCL::Mpz::mod(b, b, q);

        BICYCL::Mpz expected;
        BICYCL::Mpz::mul(expected, a, x);
        BICYCL::Mpz::add(expected, expected, b);
        BICYCL::Mpz::mod(expected, expected, q);
        if (expected != y) {
            if (errors < 5)
                std::cerr << "[check_batch_ole] mismatch at " << i
                          << ": expected=" << expected << " actual=" << y << "\n";
            ++errors;
        }
    }

    if (errors == 0) {
        std::cout << "[check_batch_ole] VERIFICATION SUCCESS: " << ns
                  << " OLE outputs correct";
        if (argc == 5)
            std::cout << " against sanitized firewall inputs";
        std::cout << "\n";
        return 0;
    }
    std::cerr << "[check_batch_ole] VERIFICATION FAILED: " << errors
              << " / " << ns << " mismatches\n";
    return 1;
}
