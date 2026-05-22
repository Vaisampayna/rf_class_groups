#pragma once
/*
 * RF-OPA over batched RF-OLE.
 *
 * Mirrors direct OPA but routes the underlying OLE sender/receiver vectors
 * through the receiver and sender reverse firewalls.
 */

#include "cg_rf_ole_batch.hpp"

#include <vector>

struct RFOpaResult {
    std::vector<BICYCL::Mpz> alpha;
    // The receiver input polynomial already evaluated at alpha. PSI2 reuses
    // this instead of evaluating pA/pB a second time after RF-OPA returns.
    std::vector<BICYCL::Mpz> input_evals;
    std::vector<BICYCL::Mpz> y_vals;
    BICYCL::Mpz q;
    double protocol_ms = 0.0;
};

struct RFOpaSendEvals {
    std::vector<BICYCL::Mpz> alpha;
    // Sender-side RF-OPA sends OLE coefficients a,b where the output is
    // a*x+b. PSI2 later needs b=u(alpha), so return both evaluated vectors.
    std::vector<BICYCL::Mpz> b_vals;
    std::vector<BICYCL::Mpz> a_vals;
    BICYCL::Mpz q;
    double protocol_ms = 0.0;
};

inline RFOpaResult rf_opa_receive_with_q(
    size_t n_pts,
    const std::vector<BICYCL::Mpz>& pB_coeffs,
    const BICYCL::Mpz& q,
    const std::string& role,
    const char* port_rec = nullptr)
{
    // RF-OPA uses the same public-point reduction as direct OPA, but the
    // underlying batched OLE call is routed through the two reverse firewalls.
    require_public_eval_points_distinct(n_pts, q);
    auto t_eval = Clock::now();
    std::vector<BICYCL::Mpz> alpha = rf_opa_eval_points(n_pts);
    std::vector<BICYCL::Mpz> x_vals =
        poly_eval_batch_auto(pB_coeffs, alpha, q);
    const double eval_ms = ms_since(t_eval);
    std::cerr << "[" << role << "] OPA setup/evaluation done in "
              << eval_ms << " ms\n";

    auto t_exchange = Clock::now();
    std::vector<BICYCL::Mpz> y_vals = rf_ole_batch_receive(x_vals, role, port_rec);
    const double exchange_ms = ms_since(t_exchange);
    std::cerr << "[" << role << "] OPA RF-OLE output-share receive done in "
              << exchange_ms << " ms\n";
    return {alpha, std::move(x_vals), std::move(y_vals), q,
            eval_ms + exchange_ms};
}

inline RFOpaResult rf_opa_receive(
    size_t n_pts,
    const std::vector<BICYCL::Mpz>& pB_coeffs,
    const std::string& role,
    const char* port_rec = nullptr)
{
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    return rf_opa_receive_with_q(n_pts, pB_coeffs, cg.cs().cleartext_bound(), role, port_rec);
}

inline RFOpaSendEvals rf_opa_send_with_q(
    size_t n_pts,
    const std::vector<BICYCL::Mpz>& pA_coeffs,
    const std::vector<BICYCL::Mpz>& rA_coeffs,
    const BICYCL::Mpz& q,
    const std::string& role,
    const char* port_rfs = nullptr)
{
    // Sender-side RF-OPA: evaluate b(.) and a(.) at public points, then send
    // those OLE sender vectors through batched RF-OLE.
    require_public_eval_points_distinct(n_pts, q);
    auto t_eval = Clock::now();
    std::vector<BICYCL::Mpz> alpha = rf_opa_eval_points(n_pts);
    std::vector<BICYCL::Mpz> b_vals =
        poly_eval_batch_auto(pA_coeffs, alpha, q);
    std::vector<BICYCL::Mpz> a_vals =
        poly_eval_batch_auto(rA_coeffs, alpha, q);
    const double eval_ms = ms_since(t_eval);
    std::cerr << "[" << role << "] OPA setup/evaluation done in "
              << eval_ms << " ms\n";

    auto t_exchange = Clock::now();
    rf_ole_batch_send(a_vals, b_vals, role, port_rfs);
    const double exchange_ms = ms_since(t_exchange);
    std::cerr << "[" << role << "] OPA RF-OLE send-share exchange done in "
              << exchange_ms << " ms\n";
    return {std::move(alpha), std::move(b_vals), std::move(a_vals), q,
            eval_ms + exchange_ms};
}

inline RFOpaSendEvals rf_opa_send(
    size_t n_pts,
    const std::vector<BICYCL::Mpz>& pA_coeffs,
    const std::vector<BICYCL::Mpz>& rA_coeffs,
    const std::string& role,
    const char* port_rfs = nullptr)
{
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    return rf_opa_send_with_q(n_pts, pA_coeffs, rA_coeffs,
                              cg.cs().cleartext_bound(), role, port_rfs);
}
