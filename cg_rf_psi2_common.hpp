#pragma once
/*
 * Common helpers for exact two-way RF-PSI.
 *
 * Defines the two parallel RF-OPA port layouts, set/polynomial utilities, and
 * plaintext exchange helpers used by both PSI2 parties.
 */

#include "cg_rf_opa.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static constexpr const char* PORT_PSI2_EXCHANGE = "9010";
static constexpr const char* PSI2_FOPA1_REC = "9003";
static constexpr const char* PSI2_FOPA1_RFR = "9002";
static constexpr const char* PSI2_FOPA1_RFS = "9001";
static constexpr const char* PSI2_FOPA2_REC = "9023";
static constexpr const char* PSI2_FOPA2_RFR = "9022";
static constexpr const char* PSI2_FOPA2_RFS = "9021";

inline const char* psi2_fopa1_rec() { return env_or_default("CG_PSI2_FOPA1_REC", PSI2_FOPA1_REC); }
inline const char* psi2_fopa1_rfr() { return env_or_default("CG_PSI2_FOPA1_RFR", PSI2_FOPA1_RFR); }
inline const char* psi2_fopa1_rfs() { return env_or_default("CG_PSI2_FOPA1_RFS", PSI2_FOPA1_RFS); }
inline const char* psi2_fopa2_rec() { return env_or_default("CG_PSI2_FOPA2_REC", PSI2_FOPA2_REC); }
inline const char* psi2_fopa2_rfr() { return env_or_default("CG_PSI2_FOPA2_RFR", PSI2_FOPA2_RFR); }
inline const char* psi2_fopa2_rfs() { return env_or_default("CG_PSI2_FOPA2_RFS", PSI2_FOPA2_RFS); }

struct PSI2Inputs {
    std::vector<BICYCL::Mpz> setA;
    std::vector<BICYCL::Mpz> setB;
    size_t mA = 0;
    size_t mB = 0;
    uint64_t seed = 42;
};

inline std::vector<BICYCL::Mpz> parse_mpz_list(const std::string& s)
{
    std::stringstream ss(s);
    std::string tok;
    std::vector<BICYCL::Mpz> out;
    while (ss >> tok)
        out.emplace_back(tok.c_str());
    return out;
}

inline void make_psi2_random_sets(
    size_t mA,
    size_t mB,
    uint64_t seed,
    const BICYCL::Mpz& q,
    std::vector<BICYCL::Mpz>& setA,
    std::vector<BICYCL::Mpz>& setB)
{
    make_random_psi_sets_full_field(mA, mB, seed, q, setA, setB);
}

inline std::vector<BICYCL::Mpz> poly_from_roots_exact_degree(
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
    mask.back() = BICYCL::Mpz(1UL);
    return poly_mul_mod(p, mask, q);
}

inline std::vector<BICYCL::Mpz> pointwise_add_mod(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    if (a.size() != b.size())
        throw std::runtime_error("pointwise_add_mod: size mismatch");
    std::vector<BICYCL::Mpz> out(a.size(), BICYCL::Mpz(0UL));
    global_pool().parallel_for(0, a.size(), [&](size_t i) {
        BICYCL::Mpz::add(out[i], a[i], b[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    }, 64);
    return out;
}

inline std::vector<BICYCL::Mpz> pointwise_sub_mod(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    if (a.size() != b.size())
        throw std::runtime_error("pointwise_sub_mod: size mismatch");
    std::vector<BICYCL::Mpz> out(a.size(), BICYCL::Mpz(0UL));
    global_pool().parallel_for(0, a.size(), [&](size_t i) {
        BICYCL::Mpz::sub(out[i], a[i], b[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    }, 64);
    return out;
}

inline std::vector<BICYCL::Mpz> pointwise_mul_mod(
    const std::vector<BICYCL::Mpz>& a,
    const std::vector<BICYCL::Mpz>& b,
    const BICYCL::Mpz& q)
{
    if (a.size() != b.size())
        throw std::runtime_error("pointwise_mul_mod: size mismatch");
    std::vector<BICYCL::Mpz> out(a.size(), BICYCL::Mpz(0UL));
    global_pool().parallel_for(0, a.size(), [&](size_t i) {
        BICYCL::Mpz::mul(out[i], a[i], b[i]);
        BICYCL::Mpz::mod(out[i], out[i], q);
    }, 64);
    return out;
}

inline void send_mpz_vec(int fd, const std::vector<BICYCL::Mpz>& v)
{
    CGNet::send_u64(fd, (uint64_t)v.size());
    for (const auto& x : v)
        CGNet::send_mpz(fd, x);
}

inline std::vector<BICYCL::Mpz> recv_mpz_vec(int fd)
{
    uint64_t n = CGNet::recv_u64(fd);
    std::vector<BICYCL::Mpz> v(n);
    for (uint64_t i = 0; i < n; ++i)
        v[i] = CGNet::recv_mpz(fd);
    return v;
}

inline void print_psi2_intersection(
    const char* role,
    const std::vector<BICYCL::Mpz>& set,
    const std::vector<BICYCL::Mpz>& evals)
{
    BICYCL::Mpz zero(0UL);
    size_t count = 0;
    const char* print_results = std::getenv("CG_PRINT_RESULTS");
    bool print_all = print_results && std::string(print_results) == "1";
    std::cout << "[" << role << "] intersection:\n";
    for (size_t i = 0; i < set.size(); ++i) {
        if (evals[i] == zero) {
            if (print_all || count < 20)
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
