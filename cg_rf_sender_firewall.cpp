/**
 * cg_rf_sender_firewall.cpp — Sender's RF for 2-round CG-AHE RF-OLE
 *
 * OPTIMISATIONS vs original:
 *   1. Precomputes Enc_pk'(0) pairs for Round 1 and Round 2 before I/O.
 *   2. Inlines ReRand with precomputed pairs.
 */
#include "cg_rf_common.hpp"
#include <iostream>

int main(int argc, char* argv[]) {
    std::cerr << "[rf_sender] init CG-AHE...\n";
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();
    const BICYCL::Mpz& rnd_bound = cs.secretkey_bound();

    const char* rfr_ip = (argc > 1) ? argv[1] : LOCALHOST;
    int rfr_fd = CGNet::connect_tcp_retry(rfr_ip, PORT_RFR);
    CG_AHE::PublicKey  pk_prime = CGNet::recv_pk(rfr_fd, cs);
    CG_AHE::CipherText ct_x    = CGNet::recv_ct(rfr_fd);
    std::cerr << "[rf_sender] R1: received (pk', ct_x') from RF_R\n";

    // Sample rho for S-RF mauling
    BICYCL::Mpz rho = rng.random_mpz(rnd_bound);
    CG_AHE::PublicKey fpk = maul_pk(pk_prime, rho, cs);

    // Precompute both ReRand pairs upfront.
    BICYCL::Mpz rr1 = rng.random_mpz(rnd_bound);
    BICYCL::QFI R1, E1;
    cs.power_of_h(R1, rr1);
    fpk.exponentiation(cs, E1, rr1); // R1 uses fpk

    BICYCL::Mpz rr2 = rng.random_mpz(rnd_bound);
    BICYCL::QFI R2, E2;
    cs.power_of_h(R2, rr2);
    pk_prime.exponentiation(cs, E2, rr2); // R2 uses pk_prime

    // Forward-maul ct_x' from pk' to fpk
    CG_AHE::CipherText ct_aligned = maul_ct_forward(ct_x, rho, cs);

    // ReRand ct_aligned under fpk (Round 1 output)
    BICYCL::QFI R_rr, E_rr;
    cs.Cl_G().nucomp(R_rr, ct_aligned.c1(), R1);
    cs.Cl_Delta().nucomp(E_rr, ct_aligned.c2(), E1);
    CG_AHE::CipherText ct_rr(R_rr, E_rr);

    int lfd = CGNet::listen_tcp(PORT_RFS);
    std::cerr << "[rf_sender] listening on :" << PORT_RFS << "\n";
    int sen_fd = CGNet::accept_one(lfd);
    close(lfd);

    CGNet::send_pk(sen_fd, fpk);
    CGNet::send_ct(sen_fd, ct_rr);
    std::cerr << "[rf_sender] R1: forwarded (fpk, ct_rr) to Sender\n";

    CG_AHE::CipherText enc_y = CGNet::recv_ct(sen_fd);
    close(sen_fd);
    std::cerr << "[rf_sender] R2: received Enc_fpk(ax+b) from Sender\n";

    // Inverse-maul enc_y from fpk back to pk'
    CG_AHE::CipherText yi_pk_prime = maul_ct_inverse(enc_y, rho, cs);

    // ReRand under pk' using precomputed pair
    BICYCL::QFI R_out, E_out;
    cs.Cl_G().nucomp(R_out, yi_pk_prime.c1(), R2);
    cs.Cl_Delta().nucomp(E_out, yi_pk_prime.c2(), E2);
    CG_AHE::CipherText enc_y_rr(R_out, E_out);

    CGNet::send_ct(rfr_fd, enc_y_rr);
    close(rfr_fd);
    std::cerr << "[rf_sender] R2: forwarded reranded Enc(ax+b) to RF_R\n";

    return 0;
}