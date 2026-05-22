/**
 * cg_rrf.cpp — Receiver's Reverse Firewall for 2-Round RF-OLE (Class-Group AHE)
 *
 * Network topology:
 *   Receiver ↔ [R-RF / FR] ↔ S-RF / FS ↔ Sender
 *
 * FR actions (samples γ):
 *   Round 1 (from Receiver → FS):
 *     - Receive (pk, c_i) from Receiver
 *     - pk'   ← MaulPK(pk, γ)
 *     - c_i'  ← MaulCT_forward(c_i, γ)      [AlignEnc: re-keys to pk']
 *     - c_i'' ← ReRand(c_i', pk')            [RerandAlignEnc]
 *     - Forward (pk', c_i'') to FS
 *
 *   Round 2 (from FS → Receiver):
 *     - Receive z' = Enc_pk'(ax+b) from FS
 *     - z    ← MaulCT_inverse(z', γ)         [AlignEnc back to pk]
 *     - z_rr ← ReRand(z, pk)                 [RerandAlignEnc]
 *     - Forward z_rr to Receiver
 *
 * Optimisation — precomputed rerandomisation noise:
 *   ReRand(c, pk) = c ⊞ Enc_pk(0; r) = (c1·h^r, c2·pk^r).
 *   The two expensive calls are power_of_h(r) and pk.exponentiation(r).
 *   Both depend only on pk' and r_i — not on the ciphertext.
 *   Once pk' is known (right after receiving pk from the Receiver and
 *   computing pk' = pk·h^γ), all n_oles pairs (h^{r_i}, pk'^{r_i}) for
 *   R1 rerandomisation are precomputed. The R1 hot loop then does only
 *   1×nupow + 3×nucomp per ciphertext.
 *
 *   R2 rerandomisation is under the original pk, also known from the start,
 *   so its noise pairs (h^{s_i}, pk^{s_i}) are precomputed too. The R2 hot
 *   loop cost is 1×nupow + 1×nucompinv + 2×nucomp.
 *
 * Usage: ./cg_rrf <recv_port> <srf_host> <srf_port>
 */

#include "cg_common.hpp"
#include <vector>

int main(int argc, char **argv) {
    const char *role = "R-RF";
    if (argc < 4) {
        fprintf(stderr, "usage: %s <recv_port> <srf_host> <srf_port>\n", argv[0]);
        return 1;
    }
    const char *recv_port = argv[1];
    const char *srf_host  = argv[2];
    const char *srf_port  = argv[3];

    RandGen rng;
    seed_randgen(rng);

    fprintf(stderr, "[R-RF] initialising CG-AHE (%zu-bit q, 128-bit sec)...\n",
            CG_COMMON_Q_NBITS);
    CG_Scheme cg = make_common_cg_scheme(rng);
    const CS &cs = cg.cs();
    fprintf(stderr, "[R-RF] ready.\n");

    // S-RF is started first and listens; R-RF connects to it before accepting
    // the Receiver so the startup ordering is race-free.
    int lfd_recv = listen_tcp(role, recv_port);
    fprintf(stderr, "[R-RF] listening on %s for Receiver…\n", recv_port);

    fprintf(stderr, "[R-RF] connecting to S-RF at %s:%s…\n", srf_host, srf_port);
    int fd_srf = connect_tcp(role, srf_host, srf_port);

    int fd_recv = accept_tcp(role, lfd_recv);
    close(lfd_recv);
    fprintf(stderr, "[R-RF] Receiver connected.\n");

    // ── Receive pk and n_oles from Receiver ───────────────────
    uint64_t n_oles = recv_u64(role, fd_recv);
    PublicKey pk = recv_pk(role, fd_recv, cs);

    // Sample γ from D_q (same distribution as the secret key).
    Mpz gamma = rng.random_mpz(cs.secretkey_bound());

    // pk' ← MaulPK(pk, γ)
    QFI h_gamma;
    cs.power_of_h(h_gamma, gamma);
    QFI pk_prime_elt;
    cs.Cl_G().nucomp(pk_prime_elt, pk.elt(), h_gamma);
    PublicKey pk_prime(cs, pk_prime_elt);

    // Forward n_oles and pk' to S-RF immediately so FS can begin its own
    // precompute pass while we compute our noise terms below.
    send_u64(role, fd_srf, n_oles);
    send_pk(role, fd_srf, pk_prime);

    // ── Precompute R1 rerandomisation noise (under pk') ───────
    // Each pair (h^{r_i}, pk'^{r_i}) is the zero-encryption randomness that
    // will be composed with c_i'' in the hot loop. power_of_h and
    // pk'.exponentiation are the two dominant costs of rerand; they are
    // data-independent and can be computed now, while the Receiver is
    // encrypting and transmitting its n_oles ciphertexts.
    std::vector<QFI> rr1_R(n_oles), rr1_E(n_oles);
    for (uint64_t i = 0; i < n_oles; ++i) {
        Mpz ri = rng.random_mpz(cs.secretkey_bound());
        cs.power_of_h(rr1_R[i], ri);
        pk_prime.exponentiation(cs, rr1_E[i], ri);
    }

    // ── Precompute R2 rerandomisation noise (under pk) ────────
    // R2 rerand is under the original pk, also known at this point.
    std::vector<QFI> rr2_R(n_oles), rr2_E(n_oles);
    for (uint64_t i = 0; i < n_oles; ++i) {
        Mpz si = rng.random_mpz(cs.secretkey_bound());
        cs.power_of_h(rr2_R[i], si);
        pk.exponentiation(cs, rr2_E[i], si);
    }

    // ── Round 1: AlignEnc + ReRand → FS ───────────────────────
    // Per-iteration cost: 1×nupow (maul, depends on ci.c1()) + 3×nucomp.
    QFI R_pow_gamma, E_mauled, R_out, E_out;
    for (uint64_t i = 0; i < n_oles; ++i) {
        CipherText ci = recv_ct(role, fd_recv);

        // AlignEnc: E_mauled = ci.c2() · ci.c1()^γ  (re-keys from pk to pk')
        cs.Cl_G().nupow(R_pow_gamma, ci.c1(), gamma);
        if (cs.compact_variant())
            cs.from_Cl_DeltaK_to_Cl_Delta(R_pow_gamma);
        cs.Cl_Delta().nucomp(E_mauled, ci.c2(), R_pow_gamma);

        // ReRand: compose with precomputed Enc_pk'(0; r_i)
        cs.Cl_G().nucomp(R_out, ci.c1(),  rr1_R[i]);
        cs.Cl_Delta().nucomp(E_out, E_mauled, rr1_E[i]);

        CipherText ci_out(R_out, E_out);
        send_ct(role, fd_srf, ci_out);
    }

    // ── Round 2: InvMaul + ReRand → Receiver ──────────────────
    // Per-iteration cost: 1×nupow (inv-maul, depends on yi.c1()) +
    // 1×nucompinv + 2×nucomp (rerand from precomputed noise).
    QFI Ry_pow_gamma, E_unmauled, R_final, E_final;
    for (uint64_t i = 0; i < n_oles; ++i) {
        CipherText yi = recv_ct(role, fd_srf);

        // InvMaul: E_unmauled = yi.c2() / yi.c1()^γ
        cs.Cl_G().nupow(Ry_pow_gamma, yi.c1(), gamma);
        if (cs.compact_variant())
            cs.from_Cl_DeltaK_to_Cl_Delta(Ry_pow_gamma);
        cs.Cl_Delta().nucompinv(E_unmauled, yi.c2(), Ry_pow_gamma);

        // ReRand under pk using precomputed noise
        cs.Cl_G().nucomp(R_final,  yi.c1(),     rr2_R[i]);
        cs.Cl_Delta().nucomp(E_final, E_unmauled, rr2_E[i]);

        CipherText yi_rr(R_final, E_final);
        send_ct(role, fd_recv, yi_rr);
    }

    uint32_t ver = recv_u32(role, fd_recv);
    send_u32(role, fd_srf, ver);

    close(fd_recv);
    close(fd_srf);
    fprintf(stderr, "[R-RF] done. verified=%u\n", ver);
    return 0;
}
