#include "cg_rf_psi2_common.hpp"
/*
 * Local algebra sanity checker for PSI2 polynomials.
 *
 * Builds deterministic PSI2 inputs and compares the polynomial test outputs
 * against the expected set intersection without running networking.
 */

#include <chrono>
#include <iostream>
#include <random>
#include <string>
#include <vector>

using HRC = std::chrono::steady_clock;

static double elapsed_ms(HRC::time_point t0)
{
    return std::chrono::duration<double, std::milli>(HRC::now() - t0).count();
}

static bool same_vec(const std::vector<BICYCL::Mpz>& a,
                     const std::vector<BICYCL::Mpz>& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i] != b[i]) {
            std::cerr << "[poly_compare] first mismatch at " << i
                      << ": a=" << a[i] << " b=" << b[i] << "\n";
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    const size_t mA = (argc > 1) ? std::stoull(argv[1]) : 1000;
    const size_t mB = (argc > 2) ? std::stoull(argv[2]) : 1000;
    const uint64_t seed = (argc > 3) ? std::stoull(argv[3]) : 42;

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    std::vector<BICYCL::Mpz> setA, setB;
    make_psi2_random_sets(mA, mB, seed, q, setA, setB);

    const size_t d = std::max(mA, mB) + 1;
    const size_t n_pts = 2 * d + 1;
    std::vector<BICYCL::Mpz> alpha = rf_opa_eval_points(n_pts);

    std::cerr << "[poly_compare] |S_A|=" << mA
              << ", |S_B|=" << mB
              << ", d=" << d
              << ", n_pts=" << n_pts
              << ", threads=" << global_pool().num_threads() << "\n";

    auto t0 = HRC::now();
    std::vector<BICYCL::Mpz> pA = poly_from_roots_exact_degree(setA, d, q, rng);
    double build_pA_ms = elapsed_ms(t0);

    t0 = HRC::now();
    std::vector<BICYCL::Mpz> eval_product =
        poly_eval_batch_product_tree(pA, alpha, q);
    double opa_product_ms = elapsed_ms(t0);

    t0 = HRC::now();
    std::vector<BICYCL::Mpz> eval_horner =
        poly_eval_batch_horner(pA, alpha, q);
    double opa_horner_ms = elapsed_ms(t0);

    const bool opa_match = same_vec(eval_product, eval_horner);

    // This models the final PSI membership-test polynomial p_intersection:
    // degree <= 2d, known either as coefficients or as values at alpha.
    std::vector<BICYCL::Mpz> p_inter = random_poly(2 * d, q, rng);

    t0 = HRC::now();
    std::vector<BICYCL::Mpz> p_inter_alpha =
        poly_eval_batch_product_tree(p_inter, alpha, q);
    double p_inter_to_alpha_ms = elapsed_ms(t0);

    t0 = HRC::now();
    std::vector<BICYCL::Mpz> final_multipoint =
        poly_eval_batch_product_tree(p_inter, setB, q);
    double final_multipoint_ms = elapsed_ms(t0);

    t0 = HRC::now();
    std::vector<BICYCL::Mpz> final_lagrange =
        lagrange_eval_batch(p_inter_alpha, setB, q);
    double final_lagrange_ms = elapsed_ms(t0);

    const bool final_match = same_vec(final_multipoint, final_lagrange);

    std::cout << "mode,stage,mA,mB,n_pts,targets,ms,match\n";
    std::cout << "common,build_pA_from_roots," << mA << "," << mB << ","
              << n_pts << "," << setA.size() << "," << build_pA_ms << ",true\n";
    std::cout << "multipoint,opa_eval_coeff_to_alpha," << mA << "," << mB << ","
              << n_pts << "," << alpha.size() << "," << opa_product_ms << ","
              << (opa_match ? "true" : "false") << "\n";
    std::cout << "horner,opa_eval_coeff_to_alpha," << mA << "," << mB << ","
              << n_pts << "," << alpha.size() << "," << opa_horner_ms << ","
              << (opa_match ? "true" : "false") << "\n";
    std::cout << "common,make_point_values_for_final_poly," << mA << "," << mB << ","
              << n_pts << "," << alpha.size() << "," << p_inter_to_alpha_ms << ",true\n";
    std::cout << "multipoint,final_eval_coeff_to_setB," << mA << "," << mB << ","
              << n_pts << "," << setB.size() << "," << final_multipoint_ms << ","
              << (final_match ? "true" : "false") << "\n";
    std::cout << "lagrange,final_eval_alpha_values_to_setB," << mA << "," << mB << ","
              << n_pts << "," << setB.size() << "," << final_lagrange_ms << ","
              << (final_match ? "true" : "false") << "\n";

    return (opa_match && final_match) ? 0 : 1;
}
