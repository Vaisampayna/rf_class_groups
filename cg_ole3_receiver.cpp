#include "cg_ole3_common.hpp"
/*
 * Direct three-round OLE receiver.
 *
 * Receives encrypted sender values, computes Enc(a*x+b+r), sends the masked
 * ciphertext back, and subtracts r from the sender's decrypted plaintext reply.
 */

#include <iostream>

int main(int argc, char** argv)
{
    size_t n_oles = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 1000;
    const char* port = (argc > 2) ? argv[2] : ole3_port_rec();
    const size_t LANES = rf_transport_lanes();

    prewarm_batch_pool();

    auto t_input = Clock::now();
    BICYCL::RandGen rng = make_secure_randgen();
    BICYCL::RandGen input_cg_rng = make_secure_randgen();
    CG_AHE::CG_Scheme input_cg = make_cg_scheme(input_cg_rng);
    BICYCL::Mpz max_val = benchmark_input_bound(input_cg.cs().cleartext_bound());
    std::vector<BICYCL::Mpz> x_vals(n_oles);
    for (auto& x : x_vals)
        x = rng.random_mpz(max_val);
    std::cerr << "[ole3_receiver] benchmark input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    // Receiver-owned protocol clock for 3-round OLE: starts after x_i inputs
    // are sampled/loaded and stops once final y_i outputs are in memory.
    auto t_protocol = Clock::now();
    BICYCL::RandGen cg_rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(cg_rng);
    const auto& cs = cg.cs();
    const BICYCL::Mpz& q = cs.cleartext_bound();

    int lfd = CGNet::listen_tcp(port);
    std::cerr << "[ole3_receiver] listening on :" << port
              << " for " << LANES << " lane(s)\n";
    std::vector<int> fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    uint64_t n_rx = CGNet::recv_u64(fds[0]);
    if (n_rx != n_oles)
        throw std::runtime_error("OLE3 receiver count mismatch");
    CG_AHE::PublicKey pk = CGNet::recv_pk(fds[0], cs);

    std::vector<CG_AHE::CipherText> enc_a(n_oles), enc_b(n_oles), enc_y(n_oles);
    recv_ct_lanes(fds, enc_a, n_oles);
    recv_ct_lanes(fds, enc_b, n_oles);

    auto& pool = global_pool();
    std::vector<BICYCL::Mpz> r_vals(n_oles), out_vals(n_oles);
    std::vector<double> enc_r_ms(n_oles), cmult_ms(n_oles), add_ms(n_oles);

    auto t_r2 = Clock::now();
    pool.parallel_for(0, n_oles, [&](size_t i) {
        thread_local BICYCL::RandGen local_rng = make_secure_randgen();
        CG_AHE::CG_Scheme& local_cg = worker_cg();
        r_vals[i] = local_rng.random_mpz(q);
        CG_AHE::ClearText r_ct(local_cg.cs(), r_vals[i]);
        auto op = Clock::now();
        CG_AHE::CipherText enc_r = local_cg.encrypt(pk, r_ct);
        enc_r_ms[i] = ms_since(op);
        op = Clock::now();
        CG_AHE::CipherText ax = local_cg.cmult(enc_a[i], x_vals[i]);
        cmult_ms[i] = ms_since(op);
        op = Clock::now();
        CG_AHE::CipherText u = local_cg.add(pk, ax, enc_b[i]);
        enc_y[i] = local_cg.add(pk, u, enc_r);
        add_ms[i] = ms_since(op);
    });
    send_ct_lanes(fds, enc_y, n_oles);
    std::cerr << "[ole3_receiver] round 2 mask/sample/compute/send y done in "
              << ms_since(t_r2) << " ms\n";
    std::cerr << "[ole3_receiver] [OPTIME] encrypt_r sum=" << sum_ms(enc_r_ms)
              << " ms, avg=" << (n_oles ? sum_ms(enc_r_ms) / (double)n_oles : 0.0) << " ms/op\n";
    std::cerr << "[ole3_receiver] [OPTIME] cmult_ax sum=" << sum_ms(cmult_ms)
              << " ms, avg=" << (n_oles ? sum_ms(cmult_ms) / (double)n_oles : 0.0) << " ms/op\n";
    std::cerr << "[ole3_receiver] [OPTIME] add_mask sum=" << sum_ms(add_ms)
              << " ms, avg=" << (n_oles ? sum_ms(add_ms) / (double)n_oles : 0.0) << " ms/op\n";

    std::vector<BICYCL::Mpz> z_vals = recv_mpz_lanes0(fds);
    if (z_vals.size() != n_oles)
        throw std::runtime_error("OLE3 receiver z count mismatch");
    for (size_t i = 0; i < n_oles; ++i) {
        BICYCL::Mpz::sub(out_vals[i], z_vals[i], r_vals[i]);
        BICYCL::Mpz::mod(out_vals[i], out_vals[i], q);
    }
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("ole3_receiver", protocol_ms);

    write_ole_receiver_io("ole3_receiver_io.txt", q, x_vals, out_vals);
    close_rf_lanes(fds);
    std::cerr << "[ole3_receiver] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
