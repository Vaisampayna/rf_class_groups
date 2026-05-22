#pragma once
/*
 * Direct OPA over batched OLE.
 *
 * OPA is implemented by evaluating sender and receiver polynomials at public
 * points alpha_i=i+1, then using batched OLE to obtain b(alpha)+a(alpha)*x.
 */

#include "cg_ope_common.hpp"

#include <string>
#include <vector>

struct OpaResult {
    std::vector<BICYCL::Mpz> alpha;
    std::vector<BICYCL::Mpz> input_evals;
    std::vector<BICYCL::Mpz> y_vals;
    BICYCL::Mpz q;
};

struct OpaSendEvals {
    std::vector<BICYCL::Mpz> alpha;
    std::vector<BICYCL::Mpz> b_vals;
    std::vector<BICYCL::Mpz> a_vals;
    BICYCL::Mpz q;
};

inline OpaResult opa_receive_with_q(
    size_t n_pts,
    const std::vector<BICYCL::Mpz>& receiver_poly_coeffs,
    const BICYCL::Mpz& q,
    const std::string& role,
    const char* port_rec = nullptr)
{
    // OPA is reduced to batched OLE at public points alpha_i=i+1.  The
    // receiver evaluates its private polynomial locally and uses those values
    // as OLE receiver inputs.
    require_public_eval_points_distinct(n_pts, q);
    auto t_eval = Clock::now();
    std::vector<BICYCL::Mpz> alpha = rf_opa_eval_points(n_pts);
    std::vector<BICYCL::Mpz> x_vals =
        poly_eval_batch_auto(receiver_poly_coeffs, alpha, q);
    std::cerr << "[" << role << "] direct OPA setup/evaluation done in "
              << ms_since(t_eval) << " ms\n";

    auto t_exchange = Clock::now();
    const char* port = port_rec ? port_rec : ope_port_rec();
    std::vector<BICYCL::Mpz> y_vals =
        direct_ole_batch_receive(x_vals, port, role);
    std::cerr << "[" << role << "] direct OPA/OLE output-share receive done in "
              << ms_since(t_exchange) << " ms\n";
    return {std::move(alpha), std::move(x_vals), std::move(y_vals), q};
}

inline OpaResult opa_receive(
    size_t n_pts,
    const std::vector<BICYCL::Mpz>& receiver_poly_coeffs,
    const std::string& role,
    const char* port_rec = nullptr)
{
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    return opa_receive_with_q(n_pts, receiver_poly_coeffs,
                              cg.cs().cleartext_bound(), role, port_rec);
}

inline OpaSendEvals opa_send_with_q(
    size_t n_pts,
    const std::vector<BICYCL::Mpz>& b_poly_coeffs,
    const std::vector<BICYCL::Mpz>& a_poly_coeffs,
    const BICYCL::Mpz& q,
    const char* receiver_host,
    const std::string& role,
    const char* port_rec = nullptr)
{
    // Sender evaluates both OLE coefficient polynomials at the same public
    // points.  The batched OLE output is b(alpha_i)+a(alpha_i)*x(alpha_i).
    require_public_eval_points_distinct(n_pts, q);
    auto t_eval = Clock::now();
    std::vector<BICYCL::Mpz> alpha = rf_opa_eval_points(n_pts);
    std::vector<BICYCL::Mpz> b_vals =
        poly_eval_batch_auto(b_poly_coeffs, alpha, q);
    std::vector<BICYCL::Mpz> a_vals =
        poly_eval_batch_auto(a_poly_coeffs, alpha, q);
    std::cerr << "[" << role << "] direct OPA setup/evaluation done in "
              << ms_since(t_eval) << " ms\n";

    auto t_exchange = Clock::now();
    const char* port = port_rec ? port_rec : ope_port_rec();
    direct_ole_batch_send(a_vals, b_vals, receiver_host, port, role);
    std::cerr << "[" << role << "] direct OPA/OLE send-share exchange done in "
              << ms_since(t_exchange) << " ms\n";
    return {std::move(alpha), std::move(b_vals), std::move(a_vals), q};
}

inline OpaSendEvals opa_send(
    size_t n_pts,
    const std::vector<BICYCL::Mpz>& b_poly_coeffs,
    const std::vector<BICYCL::Mpz>& a_poly_coeffs,
    const char* receiver_host,
    const std::string& role,
    const char* port_rec = nullptr)
{
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    return opa_send_with_q(n_pts, b_poly_coeffs, a_poly_coeffs,
                           cg.cs().cleartext_bound(), receiver_host, role,
                           port_rec);
}
