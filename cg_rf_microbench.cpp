/**
 * cg_rf_microbench.cpp — Comprehensive RF-PSI Microbenchmark
 *
 * Measures per-operation cost for every hot-path function in the RF-OLE pipeline:
 *   KeyGen, Encrypt, Decrypt, CMult, HomAdd,
 *   MaulPK, MaulCT (forward), MaulCT (inverse),
 *   ReRand-online (inline), ReRand-online (precomp only),
 *   ReRand-precomp (power_of_h), ReRand-precomp (pk.exponentiation),
 *   Full-OLE (Enc + CMult + preEnc + Add),
 *   Full-RF-OLE per element (all 4-party ops, in-process).
 *
 * Usage: ./cg_rf_microbench [iters]          (default: 50)
 */

#include "cg_rf_common.hpp"
#include "cg_thread_pool.hpp"
#include <cstdio>
#include <cstring>
#include <functional>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <thread>

using HRC = std::chrono::high_resolution_clock;

// ── Benchmark harness ─────────────────────────────────────────────────────────
struct BenchResult {
    std::string name;
    double min_us, max_us, mean_us, median_us;
    int iters;
};

static BenchResult bench(const std::string& name, int iters,
                          std::function<void()> warmup,
                          std::function<void()> fn)
{
    // warm-up
    for (int i = 0; i < std::min(iters / 5 + 1, 5); ++i) warmup();

    std::vector<double> samples(iters);
    for (int i = 0; i < iters; ++i) {
        auto t0 = HRC::now();
        fn();
        auto t1 = HRC::now();
        samples[i] = std::chrono::duration<double, std::micro>(t1 - t0).count();
    }
    std::sort(samples.begin(), samples.end());
    double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
    return {
        name,
        samples.front(),
        samples.back(),
        sum / iters,
        samples[iters / 2],
        iters
    };
}

static void print_header() {
    printf("\n%-38s  %9s  %9s  %9s  %9s  %6s\n",
           "Operation", "min µs", "median µs", "mean µs", "max µs", "iters");
    printf("%-38s  %9s  %9s  %9s  %9s  %6s\n",
           "--------------------------------------",
           "---------","---------","---------","---------","------");
}

static void print_result(const BenchResult& r) {
    printf("%-38s  %9.2f  %9.2f  %9.2f  %9.2f  %6d\n",
           r.name.c_str(), r.min_us, r.median_us, r.mean_us, r.max_us, r.iters);
}

static void print_section(const char* title) {
    printf("\n── %s ──\n", title);
    print_header();
}

int main(int argc, char** argv) {
    int iters = (argc > 1) ? std::atoi(argv[1]) : 50;

    fprintf(stderr, "=== CG-AHE RF-PSI Microbenchmark (iters=%d) ===\n", iters);
    fprintf(stderr, "Initialising CG-AHE (%zu-bit q, 128-bit sec)...\n",
            CG_Q_NBITS);

    auto t_init0 = HRC::now();
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    double init_ms = std::chrono::duration<double,std::milli>(HRC::now()-t_init0).count();
    fprintf(stderr, "Init done in %.1f ms\n\n", init_ms);

    const auto& cs = cg.cs();
    const BICYCL::Mpz& rnd_bound = cs.secretkey_bound();
    const BICYCL::Mpz& q = cs.cleartext_bound();

    // Setup keys and test values
    CG_AHE::SecretKey sk  = cg.keygen_sk();
    CG_AHE::PublicKey pk  = cg.keygen_pk(sk);

    BICYCL::Mpz r_maul  = rng.random_mpz(rnd_bound);
    BICYCL::Mpz rho     = rng.random_mpz(rnd_bound);
    CG_AHE::PublicKey pk_prime = maul_pk(pk, r_maul, cs);
    CG_AHE::PublicKey fpk      = maul_pk(pk_prime, rho, cs);

    BICYCL::Mpz m_a(12345UL), m_b(54321UL), alpha(7UL);
    CG_AHE::ClearText ct_a(cs, m_a), ct_b(cs, m_b);

    CG_AHE::CipherText enc_a   = cg.encrypt(pk,      ct_a);
    CG_AHE::CipherText enc_a_p = cg.encrypt(pk_prime, ct_a);
    CG_AHE::CipherText enc_b   = cg.encrypt(fpk,      ct_b);

    // Precomp randomizers (used for precomp-only rerand benchmark)
    BICYCL::Mpz ri = rng.random_mpz(rnd_bound);
    BICYCL::QFI pre_R1, pre_E1;
    cs.power_of_h(pre_R1, ri);
    pk.exponentiation(cs, pre_E1, ri);

    BICYCL::QFI pre_Rfpk, pre_Efpk;
    cs.power_of_h(pre_Rfpk, rng.random_mpz(rnd_bound));
    fpk.exponentiation(cs, pre_Efpk, rng.random_mpz(rnd_bound));

    // ── Section 1: Core CG-AHE Operations ─────────────────────────────────────
    print_section("1. Core CG-AHE Operations");

    {
        auto r = bench("KeyGen (sk+pk)", iters,
            [&]{ cg.keygen_sk(); },
            [&]{ auto s = cg.keygen_sk(); (void)cg.keygen_pk(s); });
        print_result(r);
    }
    {
        auto r = bench("Encrypt (under pk)", iters,
            [&]{ cg.encrypt(pk, ct_a); },
            [&]{ (void)cg.encrypt(pk, ct_a); });
        print_result(r);
    }
    {
        auto r = bench("Decrypt", iters,
            [&]{ cg.decrypt(sk, enc_a); },
            [&]{ (void)cg.decrypt(sk, enc_a); });
        print_result(r);
    }
    {
        auto r = bench("CMult (ct * scalar)", iters,
            [&]{ cg.cmult(enc_a, alpha); },
            [&]{ (void)cg.cmult(enc_a, alpha); });
        print_result(r);
    }
    {
        auto r = bench("HomAdd (pk, ct+ct)", iters,
            [&]{ cg.add(pk, enc_a, enc_b); },
            [&]{ (void)cg.add(fpk, enc_a, enc_b); });
        print_result(r);
    }
    {
        // Raw nucomp (used in HomAdd and ReRand — measure the primitive directly)
        BICYCL::QFI tmp;
        auto r = bench("nucomp (Cl_G)", iters,
            [&]{ cs.Cl_G().nucomp(tmp, enc_a.c1(), enc_b.c1()); },
            [&]{ cs.Cl_G().nucomp(tmp, enc_a.c1(), enc_b.c1()); });
        print_result(r);
    }
    {
        BICYCL::QFI tmp;
        auto r = bench("nucomp (Cl_Delta)", iters,
            [&]{ cs.Cl_Delta().nucomp(tmp, enc_a.c2(), enc_b.c2()); },
            [&]{ cs.Cl_Delta().nucomp(tmp, enc_a.c2(), enc_b.c2()); });
        print_result(r);
    }
    {
        BICYCL::QFI tmp;
        auto r = bench("nupow (Cl_G, 128-bit exp)", iters,
            [&]{ cs.Cl_G().nupow(tmp, enc_a.c1(), r_maul); },
            [&]{ cs.Cl_G().nupow(tmp, enc_a.c1(), r_maul); });
        print_result(r);
    }
    {
        BICYCL::QFI tmp;
        auto r = bench("nupow (Cl_Delta, 128-bit exp)", iters,
            [&]{ cs.Cl_Delta().nupow(tmp, enc_a.c2(), r_maul); },
            [&]{ cs.Cl_Delta().nupow(tmp, enc_a.c2(), r_maul); });
        print_result(r);
    }
    {
        BICYCL::QFI tmp;
        auto r = bench("power_of_h (h^r)", iters,
            [&]{ cs.power_of_h(tmp, ri); },
            [&]{ cs.power_of_h(tmp, ri); });
        print_result(r);
    }

    // ── Section 2: Reverse Firewall Operations ─────────────────────────────────
    print_section("2. Reverse Firewall Core Operations");

    {
        auto r = bench("MaulPK (pk -> pk')", iters,
            [&]{ maul_pk(pk, r_maul, cs); },
            [&]{ (void)maul_pk(pk, r_maul, cs); });
        print_result(r);
    }
    {
        auto r = bench("MaulCT forward (pk -> pk')", iters,
            [&]{ maul_ct_forward(enc_a, r_maul, cs); },
            [&]{ (void)maul_ct_forward(enc_a, r_maul, cs); });
        print_result(r);
    }
    {
        auto r = bench("MaulCT inverse (pk' -> pk)", iters,
            [&]{ maul_ct_inverse(enc_a_p, r_maul, cs); },
            [&]{ (void)maul_ct_inverse(enc_a_p, r_maul, cs); });
        print_result(r);
    }
    {
        // ReRand = MaulCT_forward + nucomp(c1, R) + nucomp(c2_mauled, E)
        // This is what the firewall does every iteration online
        auto r = bench("ReRand inline (maul+nucomp x2)", iters,
            [&]{
                CG_AHE::CipherText m = maul_ct_forward(enc_a, r_maul, cs);
                BICYCL::QFI Ro, Eo;
                cs.Cl_G().nucomp(Ro, m.c1(), pre_R1);
                cs.Cl_Delta().nucomp(Eo, m.c2(), pre_E1);
            },
            [&]{
                CG_AHE::CipherText m = maul_ct_forward(enc_a, r_maul, cs);
                BICYCL::QFI Ro, Eo;
                cs.Cl_G().nucomp(Ro, m.c1(), pre_R1);
                cs.Cl_Delta().nucomp(Eo, m.c2(), pre_E1);
            });
        print_result(r);
    }
    {
        EncZero pre{pre_R1, pre_E1};
        auto r = bench("Fused MaulFwd+ReRand online", iters,
            [&]{ (void)maul_fwd_rerand(enc_a, r_maul, pre, cs); },
            [&]{ (void)maul_fwd_rerand(enc_a, r_maul, pre, cs); });
        print_result(r);
    }
    {
        EncZero pre{pre_R1, pre_E1};
        auto r = bench("Fused MaulInv+ReRand online", iters,
            [&]{ (void)maul_inv_rerand(enc_a_p, r_maul, pre, cs); },
            [&]{ (void)maul_inv_rerand(enc_a_p, r_maul, pre, cs); });
        print_result(r);
    }
    {
        auto r = bench("ReRand compose only (nucomp x2)", iters,
            [&]{
                BICYCL::QFI Ro, Eo;
                cs.Cl_G().nucomp(Ro, enc_a.c1(), pre_R1);
                cs.Cl_Delta().nucomp(Eo, enc_a.c2(), pre_E1);
            },
            [&]{
                BICYCL::QFI Ro, Eo;
                cs.Cl_G().nucomp(Ro, enc_a.c1(), pre_R1);
                cs.Cl_Delta().nucomp(Eo, enc_a.c2(), pre_E1);
            });
        print_result(r);
    }
    {
        // Precomp cost: what the firewall pays per-element during precomputation
        BICYCL::QFI R, E;
        auto r = bench("ReRand precomp (h^r + pk.exp)", iters,
            [&]{
                BICYCL::Mpz ri2 = rng.random_mpz(rnd_bound);
                cs.power_of_h(R, ri2);
                pk.exponentiation(cs, E, ri2);
            },
            [&]{
                BICYCL::Mpz ri2 = rng.random_mpz(rnd_bound);
                cs.power_of_h(R, ri2);
                pk.exponentiation(cs, E, ri2);
            });
        print_result(r);
    }
    {
        // pk.exponentiation alone — the bottleneck in precomputation
        BICYCL::QFI E;
        auto r = bench("pk.exponentiation(cs, E, r)", iters,
            [&]{ pk.exponentiation(cs, E, ri); },
            [&]{ pk.exponentiation(cs, E, ri); });
        print_result(r);
    }

    // ── Section 3: Full OLE eval path ─────────────────────────────────────────
    print_section("3. Full OLE Evaluation Path (Sender)");

    {
        auto r = bench("OLE: CMult + HomAdd (online)", iters,
            [&]{
                auto t = cg.cmult(enc_a, alpha);
                (void)cg.add(fpk, t, enc_b);
            },
            [&]{
                auto t = cg.cmult(enc_a, alpha);
                (void)cg.add(fpk, t, enc_b);
            });
        print_result(r);
    }
    {
        // Full Sender per-element cost: CMult(enc_x, a) + Add(fpk, t, preEncB)
        // preEncB is already computed; this is the critical online path
        CG_AHE::CipherText pre_b = cg.encrypt(fpk, ct_b);
        auto r = bench("OLE: CMult + Add (precomp Enc(b))", iters,
            [&]{
                auto t = cg.cmult(enc_a, alpha);
                (void)cg.add(fpk, t, pre_b);
            },
            [&]{
                auto t = cg.cmult(enc_a, alpha);
                (void)cg.add(fpk, t, pre_b);
            });
        print_result(r);
    }

    // ── Section 4: Full RF-OLE per element (in-process) ───────────────────────
    print_section("4. Full 4-Party RF-OLE per Element (in-process, no network)");

    // Setup all keys and precomp pairs as firewalls would
    BICYCL::Mpz r_b   = rng.random_mpz(rnd_bound);
    BICYCL::Mpz rho_b = rng.random_mpz(rnd_bound);

    // Build pk', fpk
    BICYCL::QFI h_r_b; cs.power_of_h(h_r_b, r_b);
    BICYCL::QFI pk_prime_elt; cs.Cl_G().nucomp(pk_prime_elt, pk.elt(), h_r_b);
    CG_AHE::PublicKey pk_prime_b(cs, pk_prime_elt);

    BICYCL::QFI h_rho_b; cs.power_of_h(h_rho_b, rho_b);
    BICYCL::QFI fpk_elt; cs.Cl_G().nucomp(fpk_elt, pk_prime_elt, h_rho_b);
    CG_AHE::PublicKey fpk_b(cs, fpk_elt);

    struct EZ { BICYCL::QFI R, E; };
    EZ rrf_r1, rrf_r2, srf_r1, srf_r2;
    {
        BICYCL::Mpz tmp = rng.random_mpz(rnd_bound);
        cs.power_of_h(rrf_r1.R, tmp); pk_prime_b.exponentiation(cs, rrf_r1.E, tmp);
        tmp = rng.random_mpz(rnd_bound);
        cs.power_of_h(rrf_r2.R, tmp); pk.exponentiation(cs, rrf_r2.E, tmp);
        tmp = rng.random_mpz(rnd_bound);
        cs.power_of_h(srf_r1.R, tmp); fpk_b.exponentiation(cs, srf_r1.E, tmp);
        tmp = rng.random_mpz(rnd_bound);
        cs.power_of_h(srf_r2.R, tmp); pk_prime_b.exponentiation(cs, srf_r2.E, tmp);
    }
    CG_AHE::ClearText ct_b2(cs, m_b);
    CG_AHE::CipherText pre_b2 = cg.encrypt(fpk_b, ct_b2);

    // Verify correctness at N=1
    {
        CG_AHE::ClearText ct_xi(cs, m_a);
        CG_AHE::CipherText ci_recv = cg.encrypt(pk, ct_xi);
        // R-RF R1
        CG_AHE::CipherText ci_rrf_in = maul_ct_forward(ci_recv, r_b, cs);
        BICYCL::QFI R_out, E_out;
        cs.Cl_G().nucomp(R_out, ci_rrf_in.c1(), rrf_r1.R);
        cs.Cl_Delta().nucomp(E_out, ci_rrf_in.c2(), rrf_r1.E);
        CG_AHE::CipherText ci_rrf(R_out, E_out);
        // S-RF R1
        CG_AHE::CipherText ci_srf_in = maul_ct_forward(ci_rrf, rho_b, cs);
        cs.Cl_G().nucomp(R_out, ci_srf_in.c1(), srf_r1.R);
        cs.Cl_Delta().nucomp(E_out, ci_srf_in.c2(), srf_r1.E);
        CG_AHE::CipherText ci_srf(R_out, E_out);
        // Sender: CMult + Add
        auto t = cg.cmult(ci_srf, alpha);
        CG_AHE::CipherText yi = cg.add(fpk_b, t, pre_b2);
        // S-RF R2
        CG_AHE::CipherText yi_srf_in = maul_ct_inverse(yi, rho_b, cs);
        cs.Cl_G().nucomp(R_out, yi_srf_in.c1(), srf_r2.R);
        cs.Cl_Delta().nucomp(E_out, yi_srf_in.c2(), srf_r2.E);
        CG_AHE::CipherText yi_srf(R_out, E_out);
        // R-RF R2
        CG_AHE::CipherText yi_rrf_in = maul_ct_inverse(yi_srf, r_b, cs);
        cs.Cl_G().nucomp(R_out, yi_rrf_in.c1(), rrf_r2.R);
        cs.Cl_Delta().nucomp(E_out, yi_rrf_in.c2(), rrf_r2.E);
        CG_AHE::CipherText yi_rrf(R_out, E_out);
        // Decrypt
        CG_AHE::ClearText result = cg.decrypt(sk, yi_rrf);
        BICYCL::Mpz expected;
        BICYCL::Mpz::mul(expected, alpha, m_a);
        BICYCL::Mpz::add(expected, expected, m_b);
        BICYCL::Mpz::mod(expected, expected, q);
        if (static_cast<const BICYCL::Mpz&>(result) == expected)
            {
                char* s = mpz_get_str(nullptr, 10, (mpz_srcptr)expected);
                fprintf(stderr, "[microbench] Correctness check: PASSED (result=%s)\n", s);
                free(s);
            }
        else
            fprintf(stderr, "[microbench] Correctness check: FAILED!\n");
    }

    {
        // Full in-process RF-OLE per element with precomputed randomizers
        auto r = bench("Full RF-OLE/element (4-party, precomp, in-proc)", iters,
            [&]{
                CG_AHE::ClearText ct_xi(cs, m_a);
                CG_AHE::CipherText ci_recv = cg.encrypt(pk, ct_xi);
                CG_AHE::CipherText ci_rrf_in = maul_ct_forward(ci_recv, r_b, cs);
                BICYCL::QFI R_out, E_out;
                cs.Cl_G().nucomp(R_out, ci_rrf_in.c1(), rrf_r1.R);
                cs.Cl_Delta().nucomp(E_out, ci_rrf_in.c2(), rrf_r1.E);
                CG_AHE::CipherText ci_rrf(R_out, E_out);
                CG_AHE::CipherText ci_srf_in = maul_ct_forward(ci_rrf, rho_b, cs);
                cs.Cl_G().nucomp(R_out, ci_srf_in.c1(), srf_r1.R);
                cs.Cl_Delta().nucomp(E_out, ci_srf_in.c2(), srf_r1.E);
                CG_AHE::CipherText ci_srf(R_out, E_out);
                auto t = cg.cmult(ci_srf, alpha);
                CG_AHE::CipherText yi = cg.add(fpk_b, t, pre_b2);
                CG_AHE::CipherText yi_srf_in = maul_ct_inverse(yi, rho_b, cs);
                cs.Cl_G().nucomp(R_out, yi_srf_in.c1(), srf_r2.R);
                cs.Cl_Delta().nucomp(E_out, yi_srf_in.c2(), srf_r2.E);
                CG_AHE::CipherText yi_srf(R_out, E_out);
                CG_AHE::CipherText yi_rrf_in = maul_ct_inverse(yi_srf, r_b, cs);
                cs.Cl_G().nucomp(R_out, yi_rrf_in.c1(), rrf_r2.R);
                cs.Cl_Delta().nucomp(E_out, yi_rrf_in.c2(), rrf_r2.E);
                CG_AHE::CipherText yi_rrf(R_out, E_out);
                (void)cg.decrypt(sk, yi_rrf);
            },
            [&]{
                CG_AHE::ClearText ct_xi(cs, m_a);
                CG_AHE::CipherText ci_recv = cg.encrypt(pk, ct_xi);
                CG_AHE::CipherText ci_rrf_in = maul_ct_forward(ci_recv, r_b, cs);
                BICYCL::QFI R_out, E_out;
                cs.Cl_G().nucomp(R_out, ci_rrf_in.c1(), rrf_r1.R);
                cs.Cl_Delta().nucomp(E_out, ci_rrf_in.c2(), rrf_r1.E);
                CG_AHE::CipherText ci_rrf(R_out, E_out);
                CG_AHE::CipherText ci_srf_in = maul_ct_forward(ci_rrf, rho_b, cs);
                cs.Cl_G().nucomp(R_out, ci_srf_in.c1(), srf_r1.R);
                cs.Cl_Delta().nucomp(E_out, ci_srf_in.c2(), srf_r1.E);
                CG_AHE::CipherText ci_srf(R_out, E_out);
                auto t = cg.cmult(ci_srf, alpha);
                CG_AHE::CipherText yi = cg.add(fpk_b, t, pre_b2);
                CG_AHE::CipherText yi_srf_in = maul_ct_inverse(yi, rho_b, cs);
                cs.Cl_G().nucomp(R_out, yi_srf_in.c1(), srf_r2.R);
                cs.Cl_Delta().nucomp(E_out, yi_srf_in.c2(), srf_r2.E);
                CG_AHE::CipherText yi_srf(R_out, E_out);
                CG_AHE::CipherText yi_rrf_in = maul_ct_inverse(yi_srf, r_b, cs);
                cs.Cl_G().nucomp(R_out, yi_rrf_in.c1(), rrf_r2.R);
                cs.Cl_Delta().nucomp(E_out, yi_rrf_in.c2(), rrf_r2.E);
                CG_AHE::CipherText yi_rrf(R_out, E_out);
                (void)cg.decrypt(sk, yi_rrf);
            });

        size_t hw = std::thread::hardware_concurrency();
        print_result(r);
        printf("\n  → Projected 1000-OLE throughput (single-thread): %.1f s\n",
               r.median_us * 1000.0 / 1e6);
        printf("  → Projected 1000-OLE throughput (%zu std::threads): %.1f s\n",
               hw, r.median_us * 1000.0 / 1e6 / (double)hw);
    }

    size_t hw_threads = std::thread::hardware_concurrency();
    // Section 5: Parallel Precomputation Scaling
    // Each pool is created fresh per thread count for fair comparison.
    // The first parallel_for call on each pool warms up thread_local CG_Scheme;
    // we exclude that cost by running a warmup iteration first.
    print_section("5. Parallel Precomputation Scaling (std::thread pool)");
    printf("  Generating 1000 ReRand pairs per thread count (warmup excluded):\n");
    printf("  %7s  %12s  %12s\n", "Threads", "Wall ms", "Per-pair µs");

    for (size_t nt : {(size_t)1, (size_t)2, (size_t)4, hw_threads / 2, hw_threads}) {
        if (nt == 0 || nt > hw_threads) continue;
        struct EZp { BICYCL::QFI R, E; };
        std::vector<EZp> pre(1000);
        CGThreadPool lpool(nt);

        // Warmup: init thread_local CG_Scheme on all workers
        lpool.parallel_for(0, nt, [](size_t) {
            thread_local BICYCL::RandGen lrng = make_secure_randgen();
            thread_local BICYCL::RandGen _srng = make_secure_randgen();
            thread_local CG_AHE::CG_Scheme lcg = make_cg_scheme(_srng);
            (void)lcg;
        });

        // Timed run
        auto t0 = HRC::now();
        lpool.parallel_for(0, 1000, [&](size_t i) {
            thread_local BICYCL::RandGen lrng = make_secure_randgen();
            thread_local BICYCL::RandGen _srng = make_secure_randgen();
            thread_local CG_AHE::CG_Scheme lcg = make_cg_scheme(_srng);
            BICYCL::Mpz r2 = lrng.random_mpz(rnd_bound);
            lcg.cs().power_of_h(pre[i].R, r2);
            pk.exponentiation(lcg.cs(), pre[i].E, r2);
        });
        double ms = std::chrono::duration<double,std::milli>(HRC::now()-t0).count();
        printf("  %7zu  %12.1f  %12.2f\n", nt, ms, ms * 1000.0 / 1000.0);
    }

    printf("\n=== CG-AHE Init time: %.1f ms ===\n", init_ms);
    printf("=== Hardware: %zu std::threads available ===\n\n", hw_threads);

    return 0;
}
