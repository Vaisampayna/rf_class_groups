#include "cg_opa.hpp"
/*
 * Direct one-sided PSI sender.
 *
 * Builds p_A and a random mask polynomial, then invokes OPA over direct OLE.
 * The sender writes timing/check material; only the receiver learns the
 * intersection.
 */

#include <cstdlib>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 4) {
        std::cerr << "Usage: " << argv[0] << " <receiver_ip> <m_B> --input-file set_A.txt [--port P]\n"
                  << "   or: " << argv[0] << " <receiver_ip> <m_B> <elem1> <elem2> ... [--port P]\n"
                  << "   or: " << argv[0] << " <receiver_ip> <m_B> --random <m_A> [seed] [--port P]\n";
        return 2;
    }

    const char* receiver_ip = argv[1];
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const BICYCL::Mpz& q = cg.cs().cleartext_bound();

    size_t m_B = std::strtoull(argv[2], nullptr, 10);
    std::vector<BICYCL::Mpz> set_A;
    const char* port = ope_port_rec();

    auto t_input = Clock::now();
    for (int i = 3; i + 1 < argc; ++i) {
        if (std::string(argv[i]) == "--port")
            port = argv[i + 1];
    }

    if (std::string(argv[3]) == "--input-file") {
        if (argc < 5) {
            std::cerr << "[psi_sender] --input-file requires a path\n";
            return 2;
        }
        set_A = read_mpz_list_file(argv[4], q, "psi_sender");
        std::cerr << "[psi_sender] Read S_A from " << argv[4] << "\n";
    } else if (std::string(argv[3]) == "--random") {
        size_t m_A = (argc > 4) ? std::strtoull(argv[4], nullptr, 10) : 1000;
        uint64_t seed = (argc > 5 && std::string(argv[5]) != "--port")
            ? std::strtoull(argv[5], nullptr, 10) : 42;
        std::vector<BICYCL::Mpz> ignored_B;
        make_random_psi_sets_full_field(m_A, m_B, seed, q, set_A, ignored_B);
        std::cerr << "[psi_sender] Generated random S_A of size " << m_A
                  << " over Z_q with ~" << (std::min(m_A, m_B) / 5)
                  << " expected overlaps (benchmark seed=" << seed << ")\n";
    } else {
        for (int i = 3; i < argc; ++i) {
            if (std::string(argv[i]) == "--port" && i + 1 < argc) {
                port = argv[i + 1];
                break;
            }
            if (std::string(argv[i]) == "--input-file" && i + 1 < argc) {
                ++i;
                continue;
            }
            set_A.emplace_back(std::string(argv[i]).c_str());
        }
    }
    std::cerr << "[psi_sender] benchmark set input sampling excluded from protocol time: "
              << ms_since(t_input) << " ms\n";

    auto t_protocol = Clock::now();
    auto t_poly = Clock::now();
    size_t m_A = set_A.size();
    size_t n_pts = m_A + m_B + 1;
    std::cerr << "[psi_sender] direct OLE PSI |S_A|=" << m_A
              << ", |S_B|=" << m_B << ", n_pts=" << n_pts << "\n";

    std::vector<BICYCL::Mpz> pA = poly_from_roots(set_A, q);
    // Paper PSI masking: sample r_A and r'_A, form q_A=p_A*r'_A, and use
    // one OPA call to let the receiver obtain q_A + r_A*p_B.  For unequal
    // benchmark sizes we choose deg(r_A)=m_A and deg(r'_A)=m_B, so both
    // q_A and r_A*p_B have degree at most m_A+m_B, exactly the interpolation
    // bound covered by n_pts=m_A+m_B+1.
    std::vector<BICYCL::Mpz> rA = random_poly(m_A, q, rng);
    std::vector<BICYCL::Mpz> rA_prime = random_poly(m_B, q, rng);
    std::vector<BICYCL::Mpz> qA = poly_mul_mod(pA, rA_prime, q);
    std::cerr << "[psi_sender] vanishing polynomial and random mask polynomials generation done in "
              << ms_since(t_poly) << " ms\n";

    // Direct PSI uses exactly one OPA over direct OLE.  The sender provides
    // q_A(alpha_i) as the OLE b-vector and r_A(alpha_i) as the a-vector; the
    // receiver contributes p_B and obtains p_cap = q_A + r_A*p_B.
    auto t_exchange = Clock::now();
    opa_send_with_q(n_pts, qA, rA, q, receiver_ip, "psi_sender", port);
    std::cerr << "[psi_sender] direct OPA send-share exchange done in "
              << ms_since(t_exchange) << " ms\n";
    const double protocol_ms = ms_since(t_protocol);
    write_protocol_timing_file_from_env("psi_sender", protocol_ms);
    std::cerr << "[psi_sender] protocol end-to-end excluding input sampling done in "
              << protocol_ms << " ms\n";
    return 0;
}
