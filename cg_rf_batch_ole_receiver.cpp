#include "cg_bench_io.hpp"
/*
 * Reverse-firewalled batched OLE receiver.
 *
 * Connects to the local receiver firewall and obtains RF-OLE outputs y_i.
 * Timing starts after x_i is ready and stops when y_i is in memory.
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

    // Pre-warm the thread pool: each worker builds its thread_local CG_Scheme
    // exactly once here, before any networking. Amortised over all n_oles.
    prewarm_batch_pool();

    auto t_input = Clock::now();
    BICYCL::RandGen rng = make_secure_randgen();
    BICYCL::RandGen cg_rng = make_secure_randgen();
    CG_AHE::CG_Scheme input_cg = make_cg_scheme(cg_rng);
    BICYCL::Mpz max_val = benchmark_input_bound(input_cg.cs().cleartext_bound());

    std::vector<BICYCL::Mpz> x_vals;
    if (input_file) {
        x_vals = read_mpz_list_file_mod_q(input_file,
                                          input_cg.cs().cleartext_bound(),
                                          "batch_ole_receiver");
        if (x_vals.size() != n_oles)
            throw std::runtime_error("batch_ole_receiver: --input-file size does not match n_oles");
    } else {
        x_vals.resize(n_oles);
        for (size_t i = 0; i < n_oles; ++i)
            x_vals[i] = rng.random_mpz(max_val);
    }
    std::cerr << "[batch_ole_receiver] benchmark input loading excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    // Paper-facing clock: starts after x_i is loaded and stops after RF-OLE
    // receiver outputs are available in memory.  Offline dumps are excluded.
    auto t_online = Clock::now();
    std::cerr << "[batch_ole_receiver] Requesting " << n_oles << " RF-OLEs...\n";
    std::vector<BICYCL::Mpz> y_vals = rf_ole_batch_receive(x_vals, "batch_ole_receiver");
    std::cerr << "[batch_ole_receiver] Finished receiving " << y_vals.size() << " outputs.\n";
    const double protocol_ms = ms_since(t_online);
    std::cerr << "[batch_ole_receiver] RF-OLE protocol receive/decrypt phase done in "
              << protocol_ms << " ms\n";
    write_protocol_timing_file_from_env("batch_ole_receiver", protocol_ms);

    
    const BICYCL::Mpz& q = input_cg.cs().cleartext_bound();

    auto t_dump = Clock::now();
    write_ole_receiver_io("rf_ole_receiver_io.txt", q, x_vals, y_vals);
    std::cerr << "[batch_ole_receiver] receiver input/output written for offline checking in "
              << ms_since(t_dump) << " ms\n";
    std::cerr << "[batch_ole_receiver] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
