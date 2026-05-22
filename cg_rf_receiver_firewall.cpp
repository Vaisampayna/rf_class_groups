/**
 * cg_rf_receiver_firewall.cpp — Receiver's RF for 2-round CG-AHE RF-OLE
 *
 * Round 1 (toward Sender):
 *   1. Connect to Receiver (:9003), receive (pk, ct_x)
 *   2. Sample r_maul; pk' = pk · h^{r_maul}
 *   3. Forward-maul ct_x: ct' = (c1, c2 · c1^{r_maul})  → Enc_pk'(x)
 *   4. ReRand ct' under pk'
 *   5. Listen on :9002; forward (pk', ct_reranded) to RF_S
 *
 * Round 2 (toward Receiver):
 *   6. Receive Enc_pk'(ax+b) from RF_S
 *   7. Unmaul: ct_pk = (c1, c2 · c1^{-r_maul})  → Enc_pk(ax+b)
 *   8. ReRand under original pk; send to Receiver
 */
#include "cg_rf_common.hpp"
#include <iostream>

int main() {
    std::cerr << "[rf_receiver] init CG-AHE...\n";
    BICYCL::RandGen rng = make_secure_randgen();
    CG_AHE::CG_Scheme cg = make_cg_scheme(rng);
    const auto& cs = cg.cs();

    // ── Step 1: connect to Receiver and get (pk, ct_x) ───────────────────
    int rec_fd = CGNet::connect_tcp_retry(LOCALHOST, PORT_REC);
    CG_AHE::PublicKey pk  = CGNet::recv_pk(rec_fd, cs);
    CG_AHE::CipherText ct = CGNet::recv_ct(rec_fd);
    std::cerr << "[rf_receiver] R1: received (pk, ct_x) from Receiver\n";

    // ── Step 2: Sample r_maul, compute pk' = pk · h^{r_maul} ─────────────
    // r_maul is stored across both rounds: it creates pk' now and removes the
    // same transformation from the result later.
    BICYCL::Mpz r_maul = rng.random_mpz(cs.secretkey_bound());
    CG_AHE::PublicKey pk_prime = maul_pk(pk, r_maul, cs);
    std::cerr << "[rf_receiver] R1: computed pk'\n";

    // ── Step 3: Forward-maul ct_x: ct' = Enc_pk'(x) ──────────────────────
    // This changes the encryption key from pk to pk' without decrypting x.
    CG_AHE::CipherText ct_maul = maul_ct_forward(ct, r_maul, cs);

    // ── Step 4: ReRand ct' under pk' (adds fresh Enc_pk'(0)) ─────────────
    // Rerandomization hides the receiver's original encryption randomness.
    CG_AHE::CipherText ct_rr = cg.rerand(pk_prime, ct_maul);

    // ── Step 5: Listen on :9002, accept RF_S, send (pk', ct_rr) ──────────
    int lfd = CGNet::listen_tcp(PORT_RFR);
    std::cerr << "[rf_receiver] listening on :" << PORT_RFR << "\n";
    int rfs_fd = CGNet::accept_one(lfd);
    close(lfd);

    // Sender-side roles only receive pk' and the refreshed ciphertext.
    CGNet::send_pk(rfs_fd, pk_prime);
    CGNet::send_ct(rfs_fd, ct_rr);
    std::cerr << "[rf_receiver] R1: forwarded (pk', ct_reranded) to RF_S\n";

    // ── Step 6: Receive Enc_pk'(ax+b) from RF_S ──────────────────────────
    CG_AHE::CipherText enc_y_prime = CGNet::recv_ct(rfs_fd);
    close(rfs_fd);
    std::cerr << "[rf_receiver] R2: received Enc_pk'(ax+b) from RF_S\n";

    // ── Step 7: Unmaul: Enc_pk'(ax+b) → Enc_pk(ax+b) ────────────────────
    // Remove r_maul so the receiver's original secret key can decrypt.
    CG_AHE::CipherText enc_y_pk = maul_ct_inverse(enc_y_prime, r_maul, cs);

    // ── Step 8: ReRand under original pk, send to Receiver ───────────────
    // Final refresh before returning the ciphertext to the receiver.
    CG_AHE::CipherText enc_y_rr = cg.rerand(pk, enc_y_pk);
    CGNet::send_ct(rec_fd, enc_y_rr);
    close(rec_fd);
    std::cerr << "[rf_receiver] R2: forwarded unmaul'd Enc(ax+b) to Receiver\n";

    return 0;
}
