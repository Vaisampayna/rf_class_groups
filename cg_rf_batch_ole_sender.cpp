#include "cg_bench_io.hpp"
/*
 * Reverse-firewalled batched OLE sender.
 *
 * Provides (a_i, b_i) to the RF-OLE sender endpoint.  All network traffic goes
 * through the local sender firewall; checker dumps are outside timed work.
 */
#include "cg_rf_ole_batch.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    size_t n_oles = (argc > 1) ? std::atoi(argv[1]) : 1000;
    const char* input_file = nullptr;
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input-file" && i + 1 < argc)
            input_file = argv[++i];
    }

    // Pre-warm thread pool — one-time CG_Scheme construction per worker thread.
    prewarm_batch_pool();

    auto t_input = Clock::now();
    BICYCL::RandGen rng = make_secure_randgen();
    BICYCL::RandGen cg_rng = make_secure_randgen();
    CG_AHE::CG_Scheme input_cg = make_cg_scheme(cg_rng);
    BICYCL::Mpz max_val = benchmark_input_bound(input_cg.cs().cleartext_bound());

    std::vector<BICYCL::Mpz> a_vals;
    std::vector<BICYCL::Mpz> b_vals;
    if (input_file) {
        auto inputs = read_ole_sender_input_file(input_file,
                                                input_cg.cs().cleartext_bound(),
                                                "batch_ole_sender");
        a_vals = std::move(inputs.first);
        b_vals = std::move(inputs.second);
        if (a_vals.size() != n_oles)
            throw std::runtime_error("batch_ole_sender: --input-file size does not match n_oles");
    } else {
        a_vals.resize(n_oles);
        b_vals.resize(n_oles);
        for (size_t i = 0; i < n_oles; ++i) {
            a_vals[i] = rng.random_mpz(max_val);
            b_vals[i] = rng.random_mpz(max_val);
        }
    }
    std::cerr << "[batch_ole_sender] benchmark input loading excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    auto t_online = Clock::now();
    std::cerr << "[batch_ole_sender] Providing " << n_oles << " random RF-OLEs...\n";
    rf_ole_batch_send(a_vals, b_vals, "batch_ole_sender");
    std::cerr << "[batch_ole_sender] Finished sending.\n";
    const double protocol_ms = ms_since(t_online);
    std::cerr << "[batch_ole_sender] RF-OLE protocol send/compute phase done in "
              << protocol_ms << " ms\n";
    write_protocol_timing_file_from_env("batch_ole_sender", protocol_ms);

    auto t_dump = Clock::now();
    write_ole_sender_inputs("rf_ole_sender_inputs.txt", a_vals, b_vals);
    std::cerr << "[batch_ole_sender] sender private inputs written for offline checking in "
              << ms_since(t_dump) << " ms\n";
    std::cerr << "[batch_ole_sender] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";

    return 0;
}
