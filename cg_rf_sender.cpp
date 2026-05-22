/**
 * cg_rf_sender.cpp — Sender for 2-round CG-AHE RF-OLE
 *
 * Protocol:
 *   Connects to RF_S on :9001
 *   R1: receives (pk', Enc_pk'(x))
 *   R2: computes t=CMult(ct_x,a), u=Enc(pk',b), y=t⊞u, sends y
 *
 * Usage: ./cg_rf_sender <a> <b>
 */
#include "cg_rf_common.hpp"
#include <cstdlib>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <a> <b>\n";
        return 2;
    }
    long a_val = std::atol(argv[1]);
    long b_val = std::atol(argv[2]);

    std::cerr << "[sender] init CG-AHE...\n";
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();

    // Connect to RF_S
    int fd = CGNet::connect_tcp_retry(LOCALHOST, PORT_RFS);
    std::cerr << "[sender] connected to RF_S\n";

    // Receive (pk', Enc_pk'(x))
    // The firewall chain has already converted the receiver's ciphertext through
    // both R-RF and S-RF, creating the double-mauled key fpk. All sender
    // operations stay under fpk.
    CG_AHE::PublicKey fpk = CGNet::recv_pk(fd, cs);
    CG_AHE::CipherText ct_x   = CGNet::recv_ct(fd);
    std::cerr << "[sender] received (fpk, ct_x)\n";

    // Build a, b as ClearText
    // Parse signed integers into Mpz because CG-AHE plaintext/scalar APIs use
    // multiprecision integers.
    BICYCL::Mpz a_mpz((unsigned long)std::abs(a_val));
    if (a_val < 0) a_mpz.neg();
    BICYCL::Mpz b_mpz((unsigned long)std::abs(b_val));
    if (b_val < 0) b_mpz.neg();

    CG_AHE::ClearText b_ct(cs, b_mpz);

    // t = CMult(ct_x, a) = Enc_fpk(a*x)
    // Scalar multiplication homomorphically multiplies the hidden x by a.
    CG_AHE::CipherText t = cg.cmult(ct_x, a_mpz);

    // u = Enc(fpk, b)
    // b is encrypted directly under the same double-mauled public key.
    CG_AHE::CipherText u = cg.encrypt(fpk, b_ct);

    // y = t ⊞ u = Enc_fpk(ax + b)
    // Addition combines Enc(a*x) and Enc(b) without decryption.
    CG_AHE::CipherText y = cg.add(fpk, t, u);

    // Send y to RF_S
    CGNet::send_ct(fd, y);
    close(fd);
    std::cerr << "[sender] sent Enc(a=" << a_val << "*x + b=" << b_val << ")\n";
    return 0;
}
