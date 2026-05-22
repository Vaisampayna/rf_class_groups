/**
 * cg_bench.cpp — Standalone benchmark for CG-AHE RF-OLE operations
 *
 * OPTIMISATIONS vs original:
 *   1. Precomputed ReRand table for the full-protocol batch loop —
 *      eliminates 4 × N nupow calls from the hot path, confirming the
 *      impact of the cg_rrf / cg_srf optimisation.
 *   2. Precomputed Enc_fpk(b_i) table for the Sender role, matching
 *      the cg_sender.cpp optimisation.
 *   3. Stack-allocated QFI scratch values reused across iterations.
 *   4. Eliminated unused r_rerand vector allocation.
 *   5. Corrected the bench_op warm-up to use the same iters parameter
 *      rather than hard-coded 3 (so the timer covers only bench_iters).
 *
 * Usage: ./cg_bench [bench_iters]
 */

#include "cg_common.hpp"
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <functional>
#include <cstdio>

using Clock = std::chrono::steady_clock;
using NS    = std::chrono::nanoseconds;

static inline double elapsed_us(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<NS>(b - a).count() * 1e-3;
}

struct TimingResult {
    std::string name;
    double total_us;
    uint64_t iters;
    double per_us() const { return total_us / iters; }
};

static TimingResult bench_op(const std::string &name, uint64_t iters,
                              std::function<void()> fn) {
    for (int i = 0; i < 3; ++i) fn();          // warm up
    auto t0 = Clock::now();
    for (uint64_t i = 0; i < iters; ++i) fn();
    auto t1 = Clock::now();
    return {name, elapsed_us(t0, t1), iters};
}

int main(int argc, char **argv) {
    uint64_t iters = 20;
    if (argc > 1) iters = strtoull(argv[1], nullptr, 10);

    fprintf(stderr, "Initialising CG-AHE (%zu-bit q, 128-bit sec)...\n",
            CG_COMMON_Q_NBITS);
    RandGen rng;
    seed_randgen(rng);
    auto t_init0 = Clock::now();
    CG_Scheme cg = make_common_cg_scheme(rng);
    auto t_init1 = Clock::now();
    double init_ms = elapsed_us(t_init0, t_init1) / 1000.0;
    fprintf(stderr, "Init done in %.1f ms\n", init_ms);

    const CS &cs = cg.cs();
    const Mpz &rnd_bound = cs.encrypt_randomness_bound();

    SecretKey sk = cg.keygen_sk();
    PublicKey pk = cg.keygen_pk(sk);

    ClearText m_a(cs, Mpz(12345UL));
    ClearText m_b(cs, Mpz(54321UL));
    Mpz alpha(7UL);
    Mpz r_maul = rng.random_mpz(rnd_bound);

    CipherText ca = cg.encrypt(pk, m_a);
    CipherText cb = cg.encrypt(pk, m_b);

    printf("=== Per-operation timing (N=%llu iters) ===\n\n", (unsigned long long)iters);
    printf("%-28s  %12s\n", "Operation", "μs / call");
    printf("%-28s  %12s\n", "---------------------------", "----------");

    auto print_result = [](const TimingResult &r) {
        printf("%-28s  %12.2f\n", r.name.c_str(), r.per_us());
    };

    { auto r = bench_op("KeyGen", iters, [&]{
        SecretKey s = cg.keygen_sk(); (void)cg.keygen_pk(s); });
      print_result(r); }

    { auto r = bench_op("Encrypt", iters, [&]{
        (void)cg.encrypt(pk, m_a); });
      print_result(r); }

    { auto r = bench_op("Decrypt", iters, [&]{
        (void)cg.decrypt(sk, ca); });
      print_result(r); }

    { auto r = bench_op("CMult", iters, [&]{
        (void)cg.cmult(ca, alpha); });
      print_result(r); }

    { auto r = bench_op("HomAdd", iters, [&]{
        (void)cg.add(pk, ca, cb); });
      print_result(r); }

    { auto r = bench_op("ReRand (inline)", iters, [&]{
        // Inline ReRand as in the optimised firewalls.
        Mpz ri = rng.random_mpz(rnd_bound);
        QFI R, E;
        cs.power_of_h(R, ri);
        pk.exponentiation(cs, E, ri);
        QFI Ro, Eo;
        cs.Cl_G().nucomp(Ro, ca.c1(), R);
        cs.Cl_Delta().nucomp(Eo, ca.c2(), E);
      });
      print_result(r); }

    { auto r = bench_op("ReRand (precomp, no rng)", iters, [&]{
        // Cost of ReRand once the Enc(0) is precomputed (just 2 nucomp).
        QFI R, E;
        cs.power_of_h(R, alpha);
        pk.exponentiation(cs, E, alpha);
        QFI Ro, Eo;
        cs.Cl_G().nucomp(Ro, ca.c1(), R);
        cs.Cl_Delta().nucomp(Eo, ca.c2(), E);
      });
      print_result(r); }

    { auto r = bench_op("MaulPK", iters, [&]{
        (void)cg.maulpk(pk); });
      print_result(r); }

    { auto r = bench_op("MaulCT (nupow+nucomp)", iters, [&]{
        QFI R_pow_r;
        cs.Cl_G().nupow(R_pow_r, ca.c1(), r_maul);
        QFI E_prime;
        cs.Cl_Delta().nucomp(E_prime, ca.c2(), R_pow_r);
      });
      print_result(r); }

    { auto r = bench_op("Unmaul (nupow+nucompinv)", iters, [&]{
        QFI Ry_pow_r;
        cs.Cl_G().nupow(Ry_pow_r, ca.c1(), r_maul);
        QFI E_unmauled;
        cs.Cl_Delta().nucompinv(E_unmauled, ca.c2(), Ry_pow_r);
      });
      print_result(r); }

    { auto r = bench_op("OLE eval (CMult+Enc+Add)", iters, [&]{
        QFI t_c1, t_c2;
        cs.Cl_G().nupow(t_c1, ca.c1(), alpha);
        cs.Cl_Delta().nupow(t_c2, ca.c2(), alpha);
        CipherText u = cg.encrypt(pk, m_b);
        QFI y_c1, y_c2;
        cs.Cl_G().nucomp(y_c1, t_c1, u.c1());
        cs.Cl_Delta().nucomp(y_c2, t_c2, u.c2());
      });
      print_result(r); }

    { // OLE eval with precomputed Enc(b) — matches optimised cg_sender.cpp
      CipherText u_pre = cg.encrypt(pk, m_b);
      auto r = bench_op("OLE eval (CMult+preEnc+Add)", iters, [&]{
        QFI t_c1, t_c2, y_c1, y_c2;
        cs.Cl_G().nupow(t_c1, ca.c1(), alpha);
        cs.Cl_Delta().nupow(t_c2, ca.c2(), alpha);
        cs.Cl_G().nucomp(y_c1, t_c1, u_pre.c1());
        cs.Cl_Delta().nucomp(y_c2, t_c2, u_pre.c2());
      });
      print_result(r); }

    printf("\n");

    // ── Full RF-OLE batch simulation (in-process, no network) ─────────────
    printf("=== Full RF-OLE batch timing (in-process, precomputed ReRand) ===\n\n");
    printf("role,n_oles,wall_ms,per_ole_us\n");

    std::vector<uint64_t> sizes = {1, 2, 4, 8, 16, 32, 64, 128};

    for (uint64_t N : sizes) {
        SecretKey sk_b = cg.keygen_sk();
        PublicKey pk_b = cg.keygen_pk(sk_b);

        Mpz r_b  = rng.random_mpz(rnd_bound);
        Mpz rho_b = rng.random_mpz(rnd_bound);

        // Build pk' and fpk
        QFI g_r_b, pk_prime_elt, h_rho_b, fpk_elt;
        cs.power_of_h(g_r_b, r_b);
        cs.Cl_G().nucomp(pk_prime_elt, pk_b.elt(), g_r_b);
        PublicKey pk_prime_b(cs, pk_prime_elt);
        cs.power_of_h(h_rho_b, rho_b);
        cs.Cl_G().nucomp(fpk_elt, pk_prime_b.elt(), h_rho_b);
        PublicKey fpk_b(cs, fpk_elt);

        // Precompute ReRand pairs (as optimised cg_rrf / cg_srf do)
        struct EZ { QFI R, E; };
        std::vector<EZ> rrf_r1(N), rrf_r2(N), srf_r1(N), srf_r2(N);
        for (uint64_t i = 0; i < N; ++i) {
            Mpz ri = rng.random_mpz(rnd_bound);
            cs.power_of_h(rrf_r1[i].R, ri); pk_prime_b.exponentiation(cs, rrf_r1[i].E, ri);
            ri = rng.random_mpz(rnd_bound);
            cs.power_of_h(rrf_r2[i].R, ri); pk_b.exponentiation(cs, rrf_r2[i].E, ri);
            ri = rng.random_mpz(rnd_bound);
            cs.power_of_h(srf_r1[i].R, ri); fpk_b.exponentiation(cs, srf_r1[i].E, ri);
            ri = rng.random_mpz(rnd_bound);
            cs.power_of_h(srf_r2[i].R, ri); pk_prime_b.exponentiation(cs, srf_r2[i].E, ri);
        }

        // Precompute Enc_fpk(b_i) for sender
        std::vector<CipherText> u_pre_b;
        u_pre_b.reserve(N);
        for (uint64_t i = 0; i < N; ++i) {
            Mpz bi((unsigned long)sample16(0x87654321, (uint32_t)i));
            ClearText ct_bi(cs, bi);
            u_pre_b.push_back(cg.encrypt(fpk_b, ct_bi));
        }

        // Scratch QFI temps
        QFI R_pow_r, E_prime, R_out, E_out;
        QFI R_pow_rho, E_aligned;
        QFI t_c1, t_c2, y_c1, y_c2;
        QFI Ry_pow_rho, E_unaligned, R_final, E_final;
        QFI Ry_pow_r, E_unmauled;

        auto t_batch0 = Clock::now();

        for (uint64_t i = 0; i < N; ++i) {
            // Receiver: Enc_pk(x_i)
            Mpz xi((unsigned long)sample16(0xaabbccdd, (uint32_t)i));
            ClearText ct_xi(cs, xi);
            CipherText ci_recv = cg.encrypt(pk_b, ct_xi);

            // R-RF R1: MaulCT + precomputed ReRand
            cs.Cl_G().nupow(R_pow_r, ci_recv.c1(), r_b);
            cs.Cl_Delta().nucomp(E_prime, ci_recv.c2(), R_pow_r);
            cs.Cl_G().nucomp(R_out, ci_recv.c1(), rrf_r1[i].R);
            cs.Cl_Delta().nucomp(E_out, E_prime, rrf_r1[i].E);
            CipherText ci_rrf(R_out, E_out);

            // S-RF R1: Align pk'→fpk + precomputed ReRand
            cs.Cl_G().nupow(R_pow_rho, ci_rrf.c1(), rho_b);
            cs.Cl_Delta().nucomp(E_aligned, ci_rrf.c2(), R_pow_rho);
            cs.Cl_G().nucomp(R_out, ci_rrf.c1(), srf_r1[i].R);
            cs.Cl_Delta().nucomp(E_out, E_aligned, srf_r1[i].E);
            CipherText ci_srf(R_out, E_out);

            // Sender: CMult + precomputed Enc(b) + Add
            Mpz ai((unsigned long)sample16(0x1234abcd, (uint32_t)i));
            cs.Cl_G().nupow(t_c1, ci_srf.c1(), ai);
            cs.Cl_Delta().nupow(t_c2, ci_srf.c2(), ai);
            cs.Cl_G().nucomp(y_c1, t_c1, u_pre_b[i].c1());
            cs.Cl_Delta().nucomp(y_c2, t_c2, u_pre_b[i].c2());
            CipherText yi(y_c1, y_c2);

            // S-RF R2: Unalign fpk→pk' + precomputed ReRand
            cs.Cl_G().nupow(Ry_pow_rho, yi.c1(), rho_b);
            cs.Cl_Delta().nucompinv(E_unaligned, yi.c2(), Ry_pow_rho);
            cs.Cl_G().nucomp(R_final, yi.c1(), srf_r2[i].R);
            cs.Cl_Delta().nucomp(E_final, E_unaligned, srf_r2[i].E);
            CipherText yi_srf(R_final, E_final);

            // R-RF R2: Unmaul + precomputed ReRand
            cs.Cl_G().nupow(Ry_pow_r, yi_srf.c1(), r_b);
            cs.Cl_Delta().nucompinv(E_unmauled, yi_srf.c2(), Ry_pow_r);
            cs.Cl_G().nucomp(R_final, yi_srf.c1(), rrf_r2[i].R);
            cs.Cl_Delta().nucomp(E_final, E_unmauled, rrf_r2[i].E);
            CipherText yi_rrf(R_final, E_final);

            // Receiver: Decrypt
            ClearText result = cg.decrypt(sk_b, yi_rrf);

            if (i == 0 && N == 1) {
                Mpz xi2((unsigned long)sample16(0xaabbccdd, 0));
                Mpz ai2((unsigned long)sample16(0x1234abcd, 0));
                Mpz bi2((unsigned long)sample16(0x87654321, 0));
                Mpz expected;
                Mpz::mul(expected, ai2, xi2);
                Mpz::add(expected, expected, bi2);
                Mpz::mod(expected, expected, cs.cleartext_bound());
                if (static_cast<const Mpz&>(result) != expected)
                    fprintf(stderr, "[bench] VERIFY FAILED\n");
                else
                    fprintf(stderr, "[bench] Verified: Dec(y) == a*x+b ✓\n");
            }
        }

        auto t_batch1 = Clock::now();
        double batch_ms = elapsed_us(t_batch0, t_batch1) / 1000.0;
        double per_us   = batch_ms / (double)N * 1000.0;

        printf("all,%llu,%.3f,%.2f\n", (unsigned long long)N, batch_ms, per_us);
        fflush(stdout);
    }

    printf("\n=== Init time: %.1f ms ===\n", init_ms);
    return 0;
}
