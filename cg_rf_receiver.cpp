/**
 * cg_rf_receiver.cpp — Receiver for 2-round CG-AHE RF-OLE
 *
 * Protocol:
 *   R1: keygen, encrypt x, send (pk, ct_x) to RF_R on :9003
 *   R2: receive Enc(ax+b) back, decrypt, print result.
 *
 * Usage: ./cg_rf_receiver <x> [bits=512]
 */
#include "cg_rf_common.hpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <x> [bits=2048]\n";
        return 2;
    }
    long x_val = std::atol(argv[1]);

    std::cerr << "[receiver] init CG-AHE (" << CG_Q_NBITS << "-bit q, 128-bit sec)...\n";
    auto t0 = Clock::now();

    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();

    std::cerr << "[receiver] setup done in " << ms_since(t0) << " ms\n";

    // Keygen
    // The receiver keeps sk locally. pk is sent into the firewall chain so
    // sender-side roles can later return an encryption decryptable by sk.
    CG_AHE::SecretKey sk = cg.keygen_sk();
    CG_AHE::PublicKey pk = cg.keygen_pk(sk);

    // Encrypt x
    // Convert the signed command-line integer into BICYCL::Mpz before wrapping
    // it as a CG-AHE plaintext.
    BICYCL::Mpz x_mpz((unsigned long)std::abs(x_val));
    if (x_val < 0) x_mpz.neg();
    CG_AHE::ClearText x_ct(cs, x_mpz);
    // Round 1 receiver message: Enc_pk(x).
    CG_AHE::CipherText enc_x = cg.encrypt(pk, x_ct);

    // Listen for RF_R
    int lfd = CGNet::listen_tcp(PORT_REC);
    std::cerr << "[receiver] listening on :" << PORT_REC << "\n";
    int cfd = CGNet::accept_one(lfd);
    close(lfd);

    // Send (pk, Enc(x))
    // R-RF will maul both the public key and ciphertext before the sender sees
    // them.
    CGNet::send_pk(cfd, pk);
    CGNet::send_ct(cfd, enc_x);
    std::cerr << "[receiver] sent (pk, Enc(x=" << x_val << "))\n";

    // Receive Enc(ax+b) and decrypt
    CG_AHE::CipherText enc_y = CGNet::recv_ct(cfd);
    close(cfd);

    CG_AHE::ClearText y = cg.decrypt(sk, enc_y);
    // Expected plaintext is a*x+b for the sender's chosen a and b.
    std::cout << "[receiver] decrypted y = "
              << static_cast<const BICYCL::Mpz&>(y) << "\n";
    return 0;
}
