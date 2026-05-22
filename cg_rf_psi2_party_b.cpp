#include "cg_rf_psi2_common.hpp"
/*
 * Party B for exact two-way RF-PSI.
 *
 * Runs the complementary RF-OPA roles: receiver in F_OPA^(1) and sender in
 * F_OPA^(2), then completes the final plaintext exchange with Party A.
 */

#include <cstdlib>
#include <future>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " --random <m_A> <m_B> [seed]\n"
                  << "   or: " << argv[0] << " --file <m_A> set_B.txt\n"
                  << "   or: " << argv[0] << " <m_A> \"<S_B elems>\"\n";
        return 2;
    }

    std::vector<BICYCL::Mpz> setB;
    size_t mA = 0, mB = 0;
    uint64_t seed = 42;

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    auto t_input = Clock::now();
    if (std::string(argv[1]) == "--random") {
        mA = (argc > 2) ? std::stoull(argv[2]) : 1000;
        mB = (argc > 3) ? std::stoull(argv[3]) : 1000;
        seed = (argc > 4) ? std::stoull(argv[4]) : 42;
        std::vector<BICYCL::Mpz> ignoredA;
        make_psi2_random_sets(mA, mB, seed, q, ignoredA, setB);
    } else if (std::string(argv[1]) == "--file") {
        if (argc < 4) {
            std::cerr << "[psi2_B] --file requires m_A and set_B.txt\n";
            return 2;
        }
        mA = std::stoull(argv[2]);
        setB = read_mpz_list_file(argv[3], q, "psi2_B");
        mB = setB.size();
    } else {
        mA = std::stoull(argv[1]);
        setB = (argc > 2) ? parse_mpz_list(argv[2]) : std::vector<BICYCL::Mpz>{};
        mB = setB.size();
    }
    std::cerr << "[psi2_B] benchmark set input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    // Party-local PSI2 clock.  It starts after B's set file is loaded and
    // stops after B can output its intersection; report max(A, B) for PSI2.
    auto t_protocol = Clock::now();
    auto t_poly = Clock::now();
    const size_t d = std::max(mA, mB) + 1;
    const size_t n_pts = 2 * d + 1;
    std::vector<BICYCL::Mpz> alpha = rf_opa_eval_points(n_pts);

    std::cerr << "[psi2_B] |S_A|=" << mA << ", |S_B|=" << mB
              << ", d=" << d << ", n_pts=" << n_pts << "\n";

    std::vector<BICYCL::Mpz> pB  = poly_from_roots_exact_degree(setB, d, q, rng);
    std::vector<BICYCL::Mpz> rB  = random_poly(d, q, rng);
    std::vector<BICYCL::Mpz> rBp = random_poly(d, q, rng);
    std::vector<BICYCL::Mpz> uB  = random_poly(2 * d, q, rng);
    std::cerr << "[psi2_B] polynomial and mask setup done in "
              << ms_since(t_poly) << " ms\n";

    std::cerr << "[psi2_B] starting F_OPA^(1) and F_OPA^(2) in parallel\n";
    auto t_fopa = Clock::now();
    auto fopa1_recv = std::async(std::launch::async, [&] {
        // F_OPA^(1): A inputs (rA, uA), B inputs pB, B receives sB.
        std::cerr << "[psi2_B] F_OPA^(1): acting as receiver with pB\n";
        return rf_opa_receive_with_q(n_pts, pB, q, "psi2_B_fopa1_receiver",
                                     psi2_fopa1_rec());
    });
    auto fopa2_send = std::async(std::launch::async, [&] {
        // F_OPA^(2): B inputs (rB, uB), A inputs pA, A receives sA.
        std::cerr << "[psi2_B] F_OPA^(2): acting as sender with (rB, uB)\n";
        return rf_opa_send_with_q(n_pts, uB, rB, q, "psi2_B_fopa2_sender",
                                  psi2_fopa2_rfs());
    });

    RFOpaResult fopa1 = fopa1_recv.get();
    RFOpaSendEvals fopa2 = fopa2_send.get();
    std::cerr << "[psi2_B] parallel F_OPA pair done in "
              << ms_since(t_fopa) << " ms\n";
    const BICYCL::Mpz& q1 = fopa1.q;

    if (fopa1.input_evals.size() != n_pts || fopa1.y_vals.size() != n_pts ||
        fopa2.b_vals.size() != n_pts)
        throw std::runtime_error("psi2_B: F_OPA output size mismatch");

    const std::vector<BICYCL::Mpz>& pBv = fopa1.input_evals;
    std::vector<BICYCL::Mpz> rBpv = poly_eval_batch_auto(rBp, alpha, q1);
    const std::vector<BICYCL::Mpz>& uBv = fopa2.b_vals;

    auto t_finalize = Clock::now();
    std::cerr << "[psi2_B] waiting for sA' on :" << PORT_PSI2_EXCHANGE << "\n";
    int lfd = CGNet::listen_tcp(PORT_PSI2_EXCHANGE);
    int fd = CGNet::accept_one(lfd);
    close(lfd);

    std::vector<BICYCL::Mpz> sAp = recv_mpz_vec(fd);
    if (sAp.size() != fopa1.y_vals.size())
        throw std::runtime_error("psi2_B: sA' size mismatch");

    // pI = sA' + sB + pB*rB' - uB
    std::vector<BICYCL::Mpz> pI = pointwise_sub_mod(
        pointwise_add_mod(
            pointwise_add_mod(sAp, fopa1.y_vals, q1),
            pointwise_mul_mod(pBv, rBpv, q1),
            q1),
        uBv,
        q1);

    send_mpz_vec(fd, pI);
    close(fd);

    std::vector<BICYCL::Mpz> evalB = lagrange_eval_batch_auto(pI, setB, q1);
    const double finalize_ms = ms_since(t_finalize);
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("psi2_B", protocol_ms);

    print_psi2_intersection("psi2_B", setB, evalB);
    std::cerr << "[psi2_B] final exchange and intersection evaluation done in "
              << finalize_ms << " ms\n";
    std::cerr << "[psi2_B] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
