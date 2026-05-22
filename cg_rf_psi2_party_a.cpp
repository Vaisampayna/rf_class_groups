#include "cg_rf_psi2_common.hpp"
/*
 * Party A for exact two-way RF-PSI.
 *
 * Runs two RF-OPA instances in parallel: A is sender in F_OPA^(1) and receiver
 * in F_OPA^(2). A then exchanges final masked evaluations with B.
 */

#include <cstdlib>
#include <future>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <party_b_ip> --random <m_A> <m_B> [seed]\n"
                  << "   or: " << argv[0] << " <party_b_ip> --file set_A.txt <m_B>\n"
                  << "   or: " << argv[0] << " <party_b_ip> \"<S_A elems>\" <m_B>\n";
        return 2;
    }

    const char* party_b_ip = argv[1];
    std::vector<BICYCL::Mpz> setA;
    size_t mA = 0, mB = 0;
    uint64_t seed = 42;

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    auto t_input = Clock::now();
    if (std::string(argv[2]) == "--random") {
        mA = (argc > 3) ? std::stoull(argv[3]) : 1000;
        mB = (argc > 4) ? std::stoull(argv[4]) : 1000;
        seed = (argc > 5) ? std::stoull(argv[5]) : 42;
        std::vector<BICYCL::Mpz> ignoredB;
        make_psi2_random_sets(mA, mB, seed, q, setA, ignoredB);
    } else if (std::string(argv[2]) == "--file") {
        if (argc < 5) {
            std::cerr << "[psi2_A] --file requires set_A.txt and m_B\n";
            return 2;
        }
        setA = read_mpz_list_file(argv[3], q, "psi2_A");
        mA = setA.size();
        mB = std::stoull(argv[4]);
    } else {
        setA = parse_mpz_list(argv[2]);
        mA = setA.size();
        mB = (argc > 3) ? std::stoull(argv[3]) : mA;
    }
    std::cerr << "[psi2_A] benchmark set input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    // Party-local PSI2 clock.  The artifact reports both parties' clocks; the
    // two-way protocol completion time is the maximum of A and B.
    auto t_protocol = Clock::now();
    auto t_poly = Clock::now();
    const size_t d = std::max(mA, mB) + 1;
    const size_t n_pts = 2 * d + 1;
    std::vector<BICYCL::Mpz> alpha = rf_opa_eval_points(n_pts);

    std::cerr << "[psi2_A] |S_A|=" << mA << ", |S_B|=" << mB
              << ", d=" << d << ", n_pts=" << n_pts << "\n";

    std::vector<BICYCL::Mpz> pA  = poly_from_roots_exact_degree(setA, d, q, rng);
    std::vector<BICYCL::Mpz> rA  = random_poly(d, q, rng);
    std::vector<BICYCL::Mpz> rAp = random_poly(d, q, rng);
    std::vector<BICYCL::Mpz> uA  = random_poly(2 * d, q, rng);
    std::cerr << "[psi2_A] polynomial and mask setup done in "
              << ms_since(t_poly) << " ms\n";

    std::cerr << "[psi2_A] starting F_OPA^(1) and F_OPA^(2) in parallel\n";
    auto t_fopa = Clock::now();
    auto fopa1_send = std::async(std::launch::async, [&] {
        // F_OPA^(1): A inputs (rA, uA), B inputs pB, B receives sB.
        std::cerr << "[psi2_A] F_OPA^(1): acting as sender with (rA, uA)\n";
        return rf_opa_send_with_q(n_pts, uA, rA, q, "psi2_A_fopa1_sender",
                                  psi2_fopa1_rfs());
    });
    auto fopa2_recv = std::async(std::launch::async, [&] {
        // F_OPA^(2): B inputs (rB, uB), A inputs pA, A receives sA.
        std::cerr << "[psi2_A] F_OPA^(2): acting as receiver with pA\n";
        return rf_opa_receive_with_q(n_pts, pA, q, "psi2_A_fopa2_receiver",
                                     psi2_fopa2_rec());
    });

    RFOpaResult fopa2 = fopa2_recv.get();
    RFOpaSendEvals fopa1 = fopa1_send.get();
    std::cerr << "[psi2_A] parallel F_OPA pair done in "
              << ms_since(t_fopa) << " ms\n";
    const BICYCL::Mpz& q2 = fopa2.q;

    if (fopa1.b_vals.size() != n_pts || fopa2.input_evals.size() != n_pts ||
        fopa2.y_vals.size() != n_pts)
        throw std::runtime_error("psi2_A: F_OPA output size mismatch");

    const std::vector<BICYCL::Mpz>& uAv = fopa1.b_vals;
    const std::vector<BICYCL::Mpz>& pAv = fopa2.input_evals;
    std::vector<BICYCL::Mpz> rApv = poly_eval_batch_auto(rAp, alpha, q2);

    auto t_finalize = Clock::now();
    // sA' = sA - uA + pA*rA'
    std::vector<BICYCL::Mpz> sAp = pointwise_add_mod(
        pointwise_sub_mod(fopa2.y_vals, uAv, q2),
        pointwise_mul_mod(pAv, rApv, q2),
        q2);

    std::cerr << "[psi2_A] sending sA' to B and waiting for p_intersection\n";
    int fd = CGNet::connect_tcp_retry(party_b_ip, PORT_PSI2_EXCHANGE);
    send_mpz_vec(fd, sAp);
    std::vector<BICYCL::Mpz> pI = recv_mpz_vec(fd);
    close(fd);
    if (pI.size() != n_pts)
        throw std::runtime_error("psi2_A: p_intersection size mismatch");

    std::vector<BICYCL::Mpz> evalA = lagrange_eval_batch_auto(pI, setA, q2);
    const double finalize_ms = ms_since(t_finalize);
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("psi2_A", protocol_ms);

    print_psi2_intersection("psi2_A", setA, evalA);
    std::cerr << "[psi2_A] final exchange and intersection evaluation done in "
              << finalize_ms << " ms\n";
    std::cerr << "[psi2_A] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
