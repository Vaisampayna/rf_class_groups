/**
 * cg_rf_sender_firewall_opa.cpp — Sender's RF for OPA/PSI (persistent thread pool)
 */
#include "cg_rf_ole_batch.hpp"
#include <vector>
#include <iostream>
#include <chrono>
#include <numeric>

static double vec_sum(const std::vector<double>& xs) {
    return std::accumulate(xs.begin(), xs.end(), 0.0);
}

static void prewarm_pool(CGThreadPool& pool) {
    auto t0 = std::chrono::high_resolution_clock::now();
    pool.parallel_for(0, pool.num_threads(), [](size_t) {
        (void)worker_cg();
    });
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t0).count();
    std::cerr << "[rf_sender_opa] pool pre-warmed in " << ms << " ms\n";
}

int main(int argc, char* argv[]) {
    size_t N_THREADS = std::thread::hardware_concurrency();
    size_t LANES = rf_transport_lanes();
    size_t CHUNK = rf_chunk_size();
    auto& pool = global_pool();
    N_THREADS = pool.num_threads();
    std::cerr << "[rf_sender_opa] init CG-AHE + thread pool (" << N_THREADS
              << " workers), transport lanes=" << LANES
              << ", chunk=" << CHUNK << "...\n";

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();
    const BICYCL::Mpz& rnd_bound = cs.secretkey_bound();

    prewarm_pool(pool);

    const char* rfr_ip = (argc > 1) ? argv[1] : LOCALHOST;
    const char* port_rfr = rf_port_rfr();
    const char* port_rfs = rf_port_rfs();
    std::vector<int> rfr_fds = connect_rf_lanes(rfr_ip, port_rfr, LANES);

    int lfd = CGNet::listen_tcp(port_rfs);
    std::cerr << "[rf_sender_opa] listening on :" << port_rfs
              << " for " << LANES << " transport lane(s)\n";
    std::vector<int> sen_fds = accept_rf_lanes(lfd, LANES);
    close(lfd);

    uint64_t n_pts = CGNet::recv_u64(sen_fds[0]);
    CGNet::send_u64(rfr_fds[0], n_pts);
    std::cerr << "[rf_sender_opa] n_pts=" << n_pts << "\n";

    CG_AHE::PublicKey pk_prime = CGNet::recv_pk(rfr_fds[0], cs);
    BICYCL::Mpz rho = rng.random_mpz(rnd_bound);
    CG_AHE::PublicKey fpk = maul_pk(pk_prime, rho, cs);
    CGNet::send_pk(sen_fds[0], fpk);

    // Randomizer generation is intentionally chunked inside the forwarding
    // stream, so this timer includes both randomizer creation and traffic.
    auto t_pre = std::chrono::high_resolution_clock::now();
    std::cerr << "[rf_sender_opa] [RANDOMIZE AND FORWARD] Generating randomizers"
              << " chunk-by-chunk across " << N_THREADS << " threads...\n";

    std::vector<EncZero> pre1(n_pts), pre2(n_pts);
    std::vector<double> pre_h_ms(2 * n_pts, 0.0), pre_pkexp_ms(2 * n_pts, 0.0);

    std::cerr << "[rf_sender_opa] [CIPHERTEXT FORWARDING] Starting pipeline stream...\n";
    auto t_online = std::chrono::high_resolution_clock::now();

    std::vector<CG_AHE::CipherText> in_buf(n_pts), out_buf(n_pts);
    std::vector<double> r1_nupow(n_pts), r1_c1(n_pts), r1_c2m(n_pts), r1_c2r(n_pts);
    std::vector<double> r2_nupow(n_pts), r2_c1(n_pts), r2_c2m(n_pts), r2_c2r(n_pts);

    // Round 1: R-RF → S-RF → Sender
    for (size_t begin = 0; begin < (size_t)n_pts; begin += CHUNK) {
        size_t end = std::min((size_t)n_pts, begin + CHUNK);
        pool.parallel_for(begin, end, [&](size_t i) {
            thread_local BICYCL::RandGen local_rng = make_secure_randgen();
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            BICYCL::Mpz ri = local_rng.random_mpz(rnd_bound);
            auto op_t = Clock::now();
            local_cg.cs().power_of_h(pre1[i].R, ri);
            pre_h_ms[i] = ms_since(op_t);
            op_t = Clock::now();
            fpk.exponentiation(local_cg.cs(), pre1[i].E, ri);
            pre_pkexp_ms[i] = ms_since(op_t);
        });
        recv_ct_lanes_range(rfr_fds, in_buf, n_pts, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            MaulRerandProfile p;
            out_buf[i] = maul_fwd_rerand_profiled(in_buf[i], rho, pre1[i], cs, p);
            r1_nupow[i] = p.nupow_ms;
            r1_c1[i] = p.c1_rerand_ms;
            r1_c2m[i] = p.c2_maul_ms;
            r1_c2r[i] = p.c2_rerand_ms;
        });
        send_ct_lanes_range(sen_fds, out_buf, n_pts, begin, end);
    }
    std::cerr << "[rf_sender_opa] R1 " << n_pts << "/" << n_pts
              << " over " << LANES << " lane(s)\n";

    // Round 2: Sender → S-RF → R-RF
    for (size_t begin = 0; begin < (size_t)n_pts; begin += CHUNK) {
        size_t end = std::min((size_t)n_pts, begin + CHUNK);
        pool.parallel_for(begin, end, [&](size_t i) {
            thread_local BICYCL::RandGen local_rng = make_secure_randgen();
            CG_AHE::CG_Scheme& local_cg = worker_cg();
            BICYCL::Mpz ri = local_rng.random_mpz(rnd_bound);
            auto op_t = Clock::now();
            local_cg.cs().power_of_h(pre2[i].R, ri);
            pre_h_ms[n_pts + i] = ms_since(op_t);
            op_t = Clock::now();
            pk_prime.exponentiation(local_cg.cs(), pre2[i].E, ri);
            pre_pkexp_ms[n_pts + i] = ms_since(op_t);
        });
        recv_ct_lanes_range(sen_fds, in_buf, n_pts, begin, end);
        pool.parallel_for(begin, end, [&](size_t i) {
            MaulRerandProfile p;
            out_buf[i] = maul_inv_rerand_profiled(in_buf[i], rho, pre2[i], cs, p);
            r2_nupow[i] = p.nupow_ms;
            r2_c1[i] = p.c1_rerand_ms;
            r2_c2m[i] = p.c2_maul_ms;
            r2_c2r[i] = p.c2_rerand_ms;
        });
        send_ct_lanes_range(rfr_fds, out_buf, n_pts, begin, end);
    }
    std::cerr << "[rf_sender_opa] R2 " << n_pts << "/" << n_pts
              << " over " << LANES << " lane(s)\n";

    double online_ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t_online).count();
    double pre_ms = std::chrono::duration<double, std::milli>(
        std::chrono::high_resolution_clock::now() - t_pre).count();
    std::cerr << "[rf_sender_opa] randomizer generation and ciphertext forwarding done in " << pre_ms << " ms\n";
    std::cerr << "[rf_sender_opa] ciphertext forwarding stream done in " << online_ms << " ms\n";
    std::cerr << "[rf_sender_opa] [OPTIME] precomp power_of_h sum=" << vec_sum(pre_h_ms)
              << " ms, avg=" << (2 * n_pts ? vec_sum(pre_h_ms) / (double)(2 * n_pts) : 0.0)
              << " ms/op\n";
    std::cerr << "[rf_sender_opa] [OPTIME] precomp pk.exp sum=" << vec_sum(pre_pkexp_ms)
              << " ms, avg=" << (2 * n_pts ? vec_sum(pre_pkexp_ms) / (double)(2 * n_pts) : 0.0)
              << " ms/op\n";
    std::cerr << "[rf_sender_opa] [OPTIME] R1 maul_nupow sum=" << vec_sum(r1_nupow)
              << " ms, c1_rerand=" << vec_sum(r1_c1)
              << " ms, c2_maul=" << vec_sum(r1_c2m)
              << " ms, c2_rerand=" << vec_sum(r1_c2r) << " ms\n";
    std::cerr << "[rf_sender_opa] [OPTIME] R2 unmaul_nupow sum=" << vec_sum(r2_nupow)
              << " ms, c1_rerand=" << vec_sum(r2_c1)
              << " ms, c2_unmaul=" << vec_sum(r2_c2m)
              << " ms, c2_rerand=" << vec_sum(r2_c2r) << " ms\n";

    close_rf_lanes(sen_fds);
    close_rf_lanes(rfr_fds);
    std::cerr << "[rf_sender_opa] done\n";
    return 0;
}
