#include "cg_bench_io.hpp"
/*
 * Direct OPE receiver.
 *
 * Holds the evaluation point alpha, runs the OPE receiver reduction over
 * direct OLE, reconstructs p(alpha), then writes output for offline checking.
 */
#include "cg_ope_common.hpp"
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    size_t degree = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 1000;
    const char* alpha_file = nullptr;
    const char* port = ope_port_rec();
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--alpha-file" && i + 1 < argc) {
            alpha_file = argv[++i];
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
    BICYCL::Mpz alpha = alpha_file
        ? read_mpz_list_file_mod_q(alpha_file, q, "ope_receiver").front()
        : rng.random_mpz(benchmark_input_bound(q));
    std::vector<BICYCL::Mpz> x_vals(degree, alpha);
    std::cerr << "[ope_receiver] benchmark input loading excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    // Receiver-owned protocol clock: starts after alpha is loaded and stops
    // after the receiver has computed p(alpha) in memory.  The offline output
    // dump for correctness checking is written after this clock is captured.
    auto t_protocol = Clock::now();
    auto t_total = Clock::now();
    std::vector<BICYCL::Mpz> y_vals =
        direct_ole_batch_receive(x_vals, port, "ope_receiver", &cg);
    std::cerr << "[ope_receiver] protocol/OLE output received in "
              << ms_since(t_total) << " ms\n";

    auto t_finish = Clock::now();
    BICYCL::Mpz output = finish_ope_receiver(y_vals, alpha, q);
    std::cerr << "[ope_receiver] local Horner mask finish done in "
              << ms_since(t_finish) << " ms\n";
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("ope_receiver", protocol_ms);
    std::cerr << "[ope_receiver] protocol end-to-end excluding input loading and offline dumps done in "
              << protocol_ms << " ms\n";

    auto t_dump = Clock::now();
    write_ope_receiver_output("direct_ope_receiver_output.txt", q, alpha, output);
    std::cerr << "[ope_receiver] alpha=" << alpha << "\n";
    std::cerr << "[ope_receiver] output=" << output << "\n";
    std::cerr << "[ope_receiver] receiver output written for offline checking in "
              << ms_since(t_dump) << " ms\n";
    std::cerr << "[ope_receiver] total done in " << ms_since(t_total) << " ms\n";
    return 0;
}
