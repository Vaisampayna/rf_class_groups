/**
 * cg_rf_psi2_sim.cpp — Exact two-way PSI protocol over ideal OPA algebra.
 *
 * Local simulator for debugging PSI2 algebra before running the networked
 * RF-OPA parties. It does not perform ciphertext or socket operations.
 *
 * This is a correctness/reference implementation of the protocol:
 *   F_OPA^(1): A inputs (rA, uA), B inputs pB, B obtains sB = pB*rA + uA
 *   F_OPA^(2): B inputs (rB, uB), A inputs pA, A obtains sA = pA*rB + uB
 *   A sends sA' = sA - uA + pA*rA'
 *   B computes pI = sA' + sB + pB*rB' - uB and sends pI to A
 *   Both parties evaluate pI on their own set and output zeroes.
 *
 * It uses the same polynomial helpers as the RF-OPA/PSI code. The OPA calls
 * here are evaluated locally in the clear to pin down exact protocol algebra.
 */
#include "cg_rf_opa.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

static std::vector<BICYCL::Mpz> pointwise_add(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    std::vector<BICYCL::Mpz> out(a.size(), BICYCL::Mpz(0UL));
    for (size_t i = 0; i < a.size(); ++i) {
        BICYCL::Mpz::add(out[i], a[i], b[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    }
    return out;
}

static std::vector<BICYCL::Mpz> pointwise_sub(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    std::vector<BICYCL::Mpz> out(a.size(), BICYCL::Mpz(0UL));
    for (size_t i = 0; i < a.size(); ++i) {
        BICYCL::Mpz::sub(out[i], a[i], b[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    }
    return out;
}

static std::vector<BICYCL::Mpz> pointwise_mul(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    std::vector<BICYCL::Mpz> out(a.size(), BICYCL::Mpz(0UL));
    for (size_t i = 0; i < a.size(); ++i) {
        BICYCL::Mpz::mul(out[i], a[i], b[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    }
    return out;
}

static std::vector<BICYCL::Mpz> poly_from_roots_exact_degree(
    const std::vector<BICYCL::Mpz>& roots,
    size_t degree,
    const BICYCL::Mpz& q,
    BICYCL::RandGen& rng)
{
    std::vector<BICYCL::Mpz> p = poly_from_roots(roots, q);
    if (p.size() > degree + 1)
        throw std::runtime_error("too many roots for requested degree");

    size_t extra = degree + 1 - p.size();
    if (extra == 0) return p;

    std::vector<BICYCL::Mpz> mask = random_poly(extra, q, rng);
    mask.back() = BICYCL::Mpz(1UL); // force exact degree and non-zero leading term
    return poly_mul_mod(p, mask, q);
}

static void print_intersection(
    const char* role,
    const std::vector<BICYCL::Mpz>& set,
    const std::vector<BICYCL::Mpz>& evals)
{
    BICYCL::Mpz zero(0UL);
    size_t count = 0;
    std::cout << "[" << role << "] intersection:\n";
    for (size_t i = 0; i < set.size(); ++i) {
        if (evals[i] == zero) {
            if (count < 20)
                std::cout << "  " << set[i] << "\n";
            else if (count == 20)
                std::cout << "  ... (showing first 20 only)\n";
            ++count;
        }
    }
    if (count == 0)
        std::cout << "  (empty)\n";
    std::cout << "[" << role << "] total intersection size: " << count << "\n";
}

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " \"<S_A elems>\" \"<S_B elems>\"\n"
                  << "   or: " << argv[0] << " --random <m_A> <m_B> [seed]\n";
        return 2;
    }

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    std::vector<BICYCL::Mpz> setA, setB;
    if (std::string(argv[1]) == "--random") {
        size_t mA = (argc > 2) ? std::stoull(argv[2]) : 1000;
        size_t mB = (argc > 3) ? std::stoull(argv[3]) : 1000;
        uint64_t seed = (argc > 4) ? std::stoull(argv[4]) : 42;
        make_random_psi_sets_full_field(mA, mB, seed, q, setA, setB);
    } else {
        // Shell passes each element as its own argv entry for simple explicit mode:
        // ./cg_rf_psi2_sim --split <A...> -- <B...> is intentionally avoided here.
        std::cerr << "Use --random for now, or pass explicit sets via the existing PSI binaries.\n";
        return 2;
    }

    const size_t mA = setA.size();
    const size_t mB = setB.size();
    const size_t d = std::max(mA, mB) + 1;
    const size_t n_pts = 2 * d + 1;
    std::vector<BICYCL::Mpz> alpha = rf_opa_eval_points(n_pts);

    std::cerr << "[psi2_sim] |S_A|=" << mA << ", |S_B|=" << mB
              << ", d=" << d << ", n_pts=" << n_pts << "\n";

    std::vector<BICYCL::Mpz> pA = poly_from_roots_exact_degree(setA, d, q, rng);
    std::vector<BICYCL::Mpz> pB = poly_from_roots_exact_degree(setB, d, q, rng);
    std::vector<BICYCL::Mpz> rA  = random_poly(d, q, rng);
    std::vector<BICYCL::Mpz> rAp = random_poly(d, q, rng);
    std::vector<BICYCL::Mpz> rB  = random_poly(d, q, rng);
    std::vector<BICYCL::Mpz> rBp = random_poly(d, q, rng);
    std::vector<BICYCL::Mpz> uA  = random_poly(2 * d, q, rng);
    std::vector<BICYCL::Mpz> uB  = random_poly(2 * d, q, rng);

    std::vector<BICYCL::Mpz> pAv  = poly_eval_batch_auto(pA, alpha, q);
    std::vector<BICYCL::Mpz> pBv  = poly_eval_batch_auto(pB, alpha, q);
    std::vector<BICYCL::Mpz> rAv  = poly_eval_batch_auto(rA, alpha, q);
    std::vector<BICYCL::Mpz> rApv = poly_eval_batch_auto(rAp, alpha, q);
    std::vector<BICYCL::Mpz> rBv  = poly_eval_batch_auto(rB, alpha, q);
    std::vector<BICYCL::Mpz> rBpv = poly_eval_batch_auto(rBp, alpha, q);
    std::vector<BICYCL::Mpz> uAv  = poly_eval_batch_auto(uA, alpha, q);
    std::vector<BICYCL::Mpz> uBv  = poly_eval_batch_auto(uB, alpha, q);

    // F_OPA^(1): B receives sB = pB*rA + uA
    std::vector<BICYCL::Mpz> sB = pointwise_add(pointwise_mul(pBv, rAv, q), uAv, q);

    // F_OPA^(2): A receives sA = pA*rB + uB
    std::vector<BICYCL::Mpz> sA = pointwise_add(pointwise_mul(pAv, rBv, q), uBv, q);

    // A sends sA' = sA - uA + pA*rA'
    std::vector<BICYCL::Mpz> sAp =
        pointwise_add(pointwise_sub(sA, uAv, q), pointwise_mul(pAv, rApv, q), q);

    // B computes pI = sA' + sB + pB*rB' - uB and sends it to A.
    std::vector<BICYCL::Mpz> pI = pointwise_sub(
        pointwise_add(pointwise_add(sAp, sB, q), pointwise_mul(pBv, rBpv, q), q),
        uBv,
        q);

    std::vector<BICYCL::Mpz> evalA = lagrange_eval_batch(pI, setA, q);
    std::vector<BICYCL::Mpz> evalB = lagrange_eval_batch(pI, setB, q);

    print_intersection("Alice", setA, evalA);
    print_intersection("Bob", setB, evalB);
    return 0;
}
