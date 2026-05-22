#include "cg_ole3_common.hpp"
/*
 * Reverse-firewalled three-round OLE sender.
 *
 * Sends encrypted a,b through the sender firewall, decrypts the returned
 * masked ciphertexts, and forwards plaintext z values back through the path.
 */

#include <iostream>

int main(int argc, char** argv)
{
    size_t n_oles = (argc > 1) ? std::strtoull(argv[1], nullptr, 10) : 1000;
    const char* rfs_ip = (argc > 2) ? argv[2] : LOCALHOST;
    const char* port = (argc > 3) ? argv[3] : rf_port_rfs();
    const size_t LANES = rf_transport_lanes();

    prewarm_batch_pool();

    auto t_input = Clock::now();
    BICYCL::RandGen rng = make_secure_randgen();
    BICYCL::RandGen input_cg_rng = make_secure_randgen();
    CG_AHE::CG_Scheme input_cg = make_cg_scheme(input_cg_rng);
    BICYCL::Mpz max_val = benchmark_input_bound(input_cg.cs().cleartext_bound());
    std::vector<BICYCL::Mpz> a_vals(n_oles), b_vals(n_oles);
    for (size_t i = 0; i < n_oles; ++i) {
        a_vals[i] = rng.random_mpz(max_val);
        b_vals[i] = rng.random_mpz(max_val);
    }
    std::cerr << "[rf_ole3_sender] benchmark input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    auto t_protocol = Clock::now();
    BICYCL::RandGen cg_rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(cg_rng);
    CG_AHE::SecretKey sk = cg.keygen_sk();
    CG_AHE::PublicKey pk = cg.keygen_pk(sk);
    std::cerr << "[rf_ole3_sender] key generation done in "
              << ms_since(t_protocol) << " ms\n";

    std::vector<int> fds = connect_rf_lanes(rfs_ip, port, LANES);
    CGNet::send_u64(fds[0], (uint64_t)n_oles);
    CGNet::send_pk(fds[0], pk);

    auto& pool = global_pool();
    std::vector<CG_AHE::CipherText> enc_a(n_oles), enc_b(n_oles), enc_y(n_oles);
    std::vector<double> enc_a_ms(n_oles), enc_b_ms(n_oles), dec_ms(n_oles);

    auto t_r1 = Clock::now();
    pool.parallel_for(0, n_oles, [&](size_t i) {
        CG_AHE::CG_Scheme& local_cg = worker_cg();
        CG_AHE::ClearText a_ct(local_cg.cs(), a_vals[i]);
        auto op = Clock::now();
        enc_a[i] = local_cg.encrypt(pk, a_ct);
        enc_a_ms[i] = ms_since(op);
        CG_AHE::ClearText b_ct(local_cg.cs(), b_vals[i]);
        op = Clock::now();
        enc_b[i] = local_cg.encrypt(pk, b_ct);
        enc_b_ms[i] = ms_since(op);
    });
    send_ct_lanes(fds, enc_a, n_oles);
    send_ct_lanes(fds, enc_b, n_oles);
    std::cerr << "[rf_ole3_sender] round 1 key/encrypted a,b sent to S-RF in "
              << ms_since(t_r1) << " ms\n";
    std::cerr << "[rf_ole3_sender] [OPTIME] encrypt_a sum=" << sum_ms(enc_a_ms)
              << " ms, avg=" << (n_oles ? sum_ms(enc_a_ms) / (double)n_oles : 0.0) << " ms/op\n";
    std::cerr << "[rf_ole3_sender] [OPTIME] encrypt_b sum=" << sum_ms(enc_b_ms)
              << " ms, avg=" << (n_oles ? sum_ms(enc_b_ms) / (double)n_oles : 0.0) << " ms/op\n";

    auto t_r3 = Clock::now();
    recv_ct_lanes(fds, enc_y, n_oles);
    std::vector<BICYCL::Mpz> z_vals(n_oles);
    pool.parallel_for(0, n_oles, [&](size_t i) {
        CG_AHE::CG_Scheme& local_cg = worker_cg();
        auto op = Clock::now();
        CG_AHE::ClearText z_ct = local_cg.decrypt(sk, enc_y[i]);
        dec_ms[i] = ms_since(op);
        z_vals[i] = static_cast<const BICYCL::Mpz&>(z_ct);
    });
    send_mpz_lanes0(fds, z_vals);
    std::cerr << "[rf_ole3_sender] round 3 receive/decrypt/send masked z done in "
              << ms_since(t_r3) << " ms\n";
    std::cerr << "[rf_ole3_sender] [OPTIME] decrypt_z sum=" << sum_ms(dec_ms)
              << " ms, avg=" << (n_oles ? sum_ms(dec_ms) / (double)n_oles : 0.0) << " ms/op\n";

    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("rf_ole3_sender", protocol_ms);

    write_ole_sender_inputs("rf_ole3_sender_inputs.txt", a_vals, b_vals);
    close_rf_lanes(fds);
    std::cerr << "[rf_ole3_sender] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
