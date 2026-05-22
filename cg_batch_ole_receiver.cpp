#include "cg_bench_io.hpp"
/*
 * Direct batched OLE receiver.
 *
 * Loads or samples receiver inputs x_i, runs direct batched OLE, records the
 * receiver clock after inputs are ready and before checker dumps, then writes
 * (x_i, y_i) for offline verification.
 */
#include "cg_rf_ole_batch.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    size_t n_oles = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 1000;
    const char* port = (argc > 2) ? argv[2] : rf_port_rec();
    const char* input_file = nullptr;
    for (int i = 3; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--input-file" && i + 1 < argc)
            input_file = argv[++i];
    }
    const size_t LANES = rf_transport_lanes();

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

    // Paper-facing clock: starts after x_i is loaded, counts key generation,
    // encryption, network exchange, and decryption, and stops once y_i values
    // are available in memory.  The offline checker dump below is excluded.
    auto t_protocol = Clock::now();
    CG_AHE::CG_Scheme& cg = input_cg;
    const auto& cs = cg.cs();
    const BICYCL::Mpz& q = cs.cleartext_bound();

    CG_AHE::SecretKey sk = cg.keygen_sk();
    CG_AHE::PublicKey pk = cg.keygen_pk(sk);
    std::cerr << "[batch_ole_receiver] CG parameter initialization and key generation done in "
              << ms_since(t_protocol) << " ms\n";

    std::cerr << "[batch_ole_receiver] Direct batched OLE, n_oles=" << n_oles
              << ", lanes=" << LANES << "\n";
    int lfd = CGNet::listen_tcp(port);
    std::cerr << "[batch_ole_receiver] listening on :" << port << "\n";
    std::vector<int> fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    uint64_t n_rx = CGNet::recv_u64(fds[0]);
    if (n_rx != n_oles) {
        close_rf_lanes(fds);
        throw std::runtime_error("direct batch OLE receiver count mismatch");
    }
    CGNet::send_pk(fds[0], pk);

    auto& pool = global_pool();
    size_t NT = pool.num_threads();

    std::cerr << "[batch_ole_receiver] [ENCRYPT X] Encrypting "
              << n_oles << " inputs across " << NT << " threads...\n";
    auto t_pre = Clock::now();
    std::vector<CG_AHE::CipherText> enc_x(n_oles);
    std::vector<double> enc_ms(n_oles, 0.0);
    pool.parallel_for(0, n_oles, [&](size_t i) {
        CG_AHE::CG_Scheme& local_cg = worker_cg();
        CG_AHE::ClearText x_ct(local_cg.cs(), x_vals[i]);
        auto op_t = Clock::now();
        enc_x[i] = local_cg.encrypt(pk, x_ct);
        enc_ms[i] = ms_since(op_t);
    });
    double pre_ms = ms_since(t_pre);
    std::cerr << "[batch_ole_receiver] encrypting input ciphertexts done in " << pre_ms << " ms\n";
    std::cerr << "[batch_ole_receiver] [OPTIME] encrypt_x sum=" << sum_ms(enc_ms)
              << " ms, avg=" << (n_oles ? sum_ms(enc_ms) / (double)n_oles : 0.0)
              << " ms/op\n";

    std::cerr << "[batch_ole_receiver] [SEND/RECEIVE/DECRYPT] Starting stream...\n";
    auto t_online = Clock::now();
    send_ct_lanes(fds, enc_x, n_oles);
    std::cerr << "[batch_ole_receiver] R1 " << n_oles << "/" << n_oles
              << " OLE inputs sent\n";

    std::vector<CG_AHE::CipherText> enc_y(n_oles);
    recv_ct_lanes(fds, enc_y, n_oles);

    std::vector<BICYCL::Mpz> y_vals(n_oles);
    std::vector<double> dec_ms(n_oles, 0.0);
    pool.parallel_for(0, n_oles, [&](size_t i) {
        CG_AHE::CG_Scheme& local_cg = worker_cg();
        auto op_t = Clock::now();
        CG_AHE::ClearText y_ct = local_cg.decrypt(sk, enc_y[i]);
        dec_ms[i] = ms_since(op_t);
        y_vals[i] = static_cast<const BICYCL::Mpz&>(y_ct);
    });
    double online_ms = ms_since(t_online);
    std::cerr << "[batch_ole_receiver] R2 " << n_oles << "/" << n_oles
              << " OLE outputs received\n";
    std::cerr << "[batch_ole_receiver] sending ciphertexts, receiving outputs, and decrypting done in " << online_ms << " ms\n";
    std::cerr << "[batch_ole_receiver] [OPTIME] decrypt_y sum=" << sum_ms(dec_ms)
              << " ms, avg=" << (n_oles ? sum_ms(dec_ms) / (double)n_oles : 0.0)
              << " ms/op\n";

    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("batch_ole_receiver", protocol_ms);

    auto t_dump = Clock::now();
    write_ole_receiver_io("direct_ole_receiver_io.txt", q, x_vals, y_vals);
    std::cerr << "[batch_ole_receiver] receiver input/output written for offline checking in "
              << ms_since(t_dump) << " ms\n";
    close_rf_lanes(fds);
    std::cerr << "[batch_ole_receiver] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
