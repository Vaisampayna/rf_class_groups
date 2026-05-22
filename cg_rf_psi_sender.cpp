/**
 * cg_rf_psi_sender.cpp — Sender for CG-AHE RF-PSI
 *
 * Preferred mode:
 *   ./cg_rf_psi_sender <m_B> --input-file set_A.txt
 * Optional benchmark modes are kept for quick local experiments.
 */
#include "cg_rf_opa.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <m_B> --input-file set_A.txt\n"
                  << "   or: " << argv[0] << " <m_B> <elem1> <elem2> ...\n"
                  << "   or: " << argv[0] << " <m_B> --random <m_A> [seed]\n";
        return 2;
    }

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();
    const BICYCL::Mpz& q = cs.cleartext_bound();

    size_t m_B = std::stoull(argv[1]);
    std::vector<BICYCL::Mpz> set_A;

    auto t_input = Clock::now();
    if (std::string(argv[2]) == "--input-file") {
        if (argc < 4) {
            std::cerr << "[psi_sender] --input-file requires a path\n";
            return 2;
        }
        set_A = read_mpz_list_file(argv[3], q, "psi_sender");
        std::cerr << "[psi_sender] Read S_A from " << argv[3] << "\n";
    } else if (std::string(argv[2]) == "--random") {
        size_t m_A = (argc > 3) ? std::stoull(argv[3]) : 1000;
        uint64_t seed = (argc > 4) ? std::stoull(argv[4]) : 42;
        std::vector<BICYCL::Mpz> ignored_B;
        make_random_psi_sets_full_field(m_A, m_B, seed, q, set_A, ignored_B);

        std::cerr << "[psi_sender] Generated random S_A of size " << m_A
                  << " over Z_q with ~" << (std::min(m_A, m_B) / 5)
                  << " expected overlaps (benchmark seed=" << seed << ")\n";
    } else {
        for (int i = 2; i < argc; ++i)
            set_A.emplace_back(std::string(argv[i]).c_str());
    }
    std::cerr << "[psi_sender] benchmark set input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    auto t_poly = Clock::now();
    size_t m_A = set_A.size();
    size_t n_pts = m_A + m_B + 1;
    std::cerr << "[psi_sender] |S_A|=" << m_A << ", |S_B|=" << m_B
              << ", n_pts=" << n_pts << "\n";

    std::vector<BICYCL::Mpz> pA = poly_from_roots(set_A, q);
    // Paper PSI masking: sample r_A and r'_A, form q_A=p_A*r'_A, and use
    // one RF-OPA call to let the receiver obtain q_A + r_A*p_B.  For unequal
    // benchmark sizes we choose deg(r_A)=m_A and deg(r'_A)=m_B, so both
    // q_A and r_A*p_B have degree at most m_A+m_B, exactly the interpolation
    // bound covered by n_pts=m_A+m_B+1.
    std::vector<BICYCL::Mpz> rA = random_poly(m_A, q, rng);
    std::vector<BICYCL::Mpz> rA_prime = random_poly(m_B, q, rng);
    std::vector<BICYCL::Mpz> qA = poly_mul_mod(pA, rA_prime, q);
    const double poly_ms = ms_since(t_poly);
    std::cerr << "[psi_sender] vanishing polynomial and random mask polynomials generation done in "
              << poly_ms << " ms\n";

    // RF-PSI is exactly the direct one-way PSI OPA call, with batched RF-OLE
    // underneath.  The sender contributes q_A as the b-polynomial and r_A as
    // the a-polynomial, so the receiver obtains p_cap = q_A + r_A*p_B.
    (void)cs;
    auto t_exchange = Clock::now();
    RFOpaSendEvals sent = rf_opa_send(n_pts, qA, rA, "psi_sender");
    std::cerr << "[psi_sender] RF-OPA send-share exchange done in "
              << ms_since(t_exchange) << " ms\n";
    const double protocol_ms = poly_ms + sent.protocol_ms;
    write_protocol_timing_file_from_env("psi_sender", protocol_ms);
    std::cerr << "[psi_sender] done\n";
    std::cerr << "[psi_sender] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
