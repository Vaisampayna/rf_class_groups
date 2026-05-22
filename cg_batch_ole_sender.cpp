#include "cg_bench_io.hpp"
/*
 * Direct batched OLE sender.
 *
 * Loads or samples sender coefficients (a_i, b_i), receives Enc(x_i), returns
 * Enc(a_i*x_i+b_i), and records sender time before writing checker inputs.
 */
#include "cg_rf_ole_batch.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    size_t n_oles = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 1000;
    const char* receiver_ip = (argc > 2) ? argv[2] : LOCALHOST;
    const char* port = (argc > 3) ? argv[3] : rf_port_rec();
    const char* input_file = nullptr;
    for (int i = 4; i < argc; ++i) {
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

    auto t_protocol = Clock::now();
    CG_AHE::CG_Scheme& cg = input_cg;
    const auto& cs = cg.cs();
    std::cerr << "[batch_ole_sender] CG parameter initialization done in "
              << ms_since(t_protocol) << " ms\n";

    std::cerr << "[batch_ole_sender] Direct batched OLE, n_oles=" << n_oles
              << ", lanes=" << LANES << "\n";
    std::vector<int> fds = connect_rf_lanes(receiver_ip, port, LANES);
    std::cerr << "[batch_ole_sender] connected to receiver at "
              << receiver_ip << ":" << port << "\n";

    CGNet::send_u64(fds[0], (uint64_t)n_oles);
    CG_AHE::PublicKey pk = CGNet::recv_pk(fds[0], cs);

    auto& pool = global_pool();
    size_t NT = pool.num_threads();

    std::cerr << "[batch_ole_sender] [ENCRYPT B] Encrypting "
              << n_oles << " B-values across " << NT << " threads...\n";
    auto t_pre = Clock::now();
    std::vector<CG_AHE::CipherText> enc_b(n_oles);
    std::vector<double> enc_b_ms(n_oles, 0.0);
    pool.parallel_for(0, n_oles, [&](size_t i) {
        CG_AHE::CG_Scheme& local_cg = worker_cg();
        CG_AHE::ClearText b_ct(local_cg.cs(), b_vals[i]);
        auto op_t = Clock::now();
        enc_b[i] = local_cg.encrypt(pk, b_ct);
        enc_b_ms[i] = ms_since(op_t);
    });
    double pre_ms = ms_since(t_pre);
    std::cerr << "[batch_ole_sender] encrypting B ciphertexts done in " << pre_ms << " ms\n";
    std::cerr << "[batch_ole_sender] [OPTIME] encrypt_b sum=" << sum_ms(enc_b_ms)
              << " ms, avg=" << (n_oles ? sum_ms(enc_b_ms) / (double)n_oles : 0.0)
              << " ms/op\n";

    std::cerr << "[batch_ole_sender] [RECEIVE/COMPUTE/SEND] Starting stream...\n";
    auto t_online = Clock::now();
    std::vector<CG_AHE::CipherText> enc_x(n_oles), enc_y(n_oles);
    recv_ct_lanes(fds, enc_x, n_oles);

    std::vector<double> cmult_ms(n_oles, 0.0), add_ms(n_oles, 0.0);
    pool.parallel_for(0, n_oles, [&](size_t i) {
        CG_AHE::CG_Scheme& local_cg = worker_cg();
        auto op_t = Clock::now();
        CG_AHE::CipherText ax = local_cg.cmult(enc_x[i], a_vals[i]);
        cmult_ms[i] = ms_since(op_t);
        op_t = Clock::now();
        enc_y[i] = local_cg.add(pk, ax, enc_b[i]);
        add_ms[i] = ms_since(op_t);
    });

    send_ct_lanes(fds, enc_y, n_oles);
    double online_ms = ms_since(t_online);
    std::cerr << "[batch_ole_sender] " << n_oles << "/" << n_oles
              << " OLEs done\n";
    std::cerr << "[batch_ole_sender] receiving ciphertexts, computing responses, and sending ciphertexts done in " << online_ms << " ms\n";
    std::cerr << "[batch_ole_sender] [OPTIME] cmult sum=" << sum_ms(cmult_ms)
              << " ms, avg=" << (n_oles ? sum_ms(cmult_ms) / (double)n_oles : 0.0)
              << " ms/op\n";
    std::cerr << "[batch_ole_sender] [OPTIME] add sum=" << sum_ms(add_ms)
              << " ms, avg=" << (n_oles ? sum_ms(add_ms) / (double)n_oles : 0.0)
              << " ms/op\n";

    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("batch_ole_sender", protocol_ms);

    auto t_dump = Clock::now();
    write_ole_sender_inputs("direct_ole_sender_inputs.txt", a_vals, b_vals);
    std::cerr << "[batch_ole_sender] sender private inputs written for offline checking in "
              << ms_since(t_dump) << " ms\n";

    close_rf_lanes(fds);
    std::cerr << "[batch_ole_sender] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
