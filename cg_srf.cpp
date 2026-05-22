/**
 * cg_srf.cpp — Sender's Reverse Firewall (FS) for 2-Round RF-OLE (Class-Group AHE)
 *
 * Network topology:
 *   Receiver ↔ R-RF ↔ [S-RF / FS] ↔ Sender
 *
 * FS samples blinding ρ and performs the full RerandAlignEnc in both rounds:
 *   Round 1 (R-RF → Sender):
 *     - Receive (pk', c_i'') from R-RF
 *     - fpk    ← MaulPK(pk', ρ)
 *     - f⟨x_i⟩ ← AlignEnc(c_i'', ρ) then ReRand(·, fpk)
 *     - Forward (fpk, f⟨x_i⟩) to Sender
 *
 *   Round 2 (Sender → R-RF):
 *     - Receive ez_i = Enc_fpk(a_i·x_i + b_i) from Sender
 *     - z'_i ← InvAlignEnc(ez_i, ρ) then ReRand(·, pk')
 *     - Forward z'_i to R-RF
 *
 * Optimisation — precomputed rerandomisation noise:
 *   Both R1 (under fpk) and R2 (under pk') rerandomisations require
 *   power_of_h(r) and pk.exponentiation(r) per ciphertext. Neither
 *   depends on the ciphertext — only on the key and the random scalar.
 *
 *   FS receives pk' from R-RF and immediately computes fpk = pk'·h^ρ.
 *   At that point both keys are known, so all 2×n_oles noise pairs:
 *     R1: (h^{r_i},  fpk^{r_i})   for i = 0..n_oles-1
 *     R2: (h^{s_i}, pk'^{s_i})   for i = 0..n_oles-1
 *   are precomputed before the first ciphertext arrives. The hot loops
 *   then contain only nucomp calls (plus the unavoidable nupow for
 *   AlignEnc/InvAlignEnc which depends on c1).
 *
 * Usage: ./cg_srf <rrf_port> <sender_host> <sender_port>
 */

#include "cg_common.hpp"
#include <vector>

int main(int argc, char **argv) {
    const char *role = "S-RF";
    if (argc < 4) {
        fprintf(stderr, "usage: %s <rrf_port> <sender_host> <sender_port>\n", argv[0]);
        return 1;
    }
    const char *rrf_port    = argv[1];
    const char *sender_host = argv[2];
    const char *sender_port = argv[3];

    RandGen rng;
    seed_randgen(rng);

    fprintf(stderr, "[S-RF] initialising CG-AHE (%zu-bit q, 128-bit sec)...\n",
            CG_COMMON_Q_NBITS);
    CG_Scheme cg = make_common_cg_scheme(rng);
    const CS &cs = cg.cs();
    fprintf(stderr, "[S-RF] ready.\n");

    int lfd_rrf = listen_tcp(role, rrf_port);
    fprintf(stderr, "[S-RF] listening on %s for R-RF…\n", rrf_port);

    fprintf(stderr, "[S-RF] connecting to Sender at %s:%s…\n", sender_host, sender_port);
    int fd_sender = connect_tcp(role, sender_host, sender_port);

    int fd_rrf = accept_tcp(role, lfd_rrf);
    close(lfd_rrf);
    fprintf(stderr, "[S-RF] R-RF connected.\n");

    // ── Receive (n_oles, pk') from R-RF ───────────────────────
    uint64_t n_oles    = recv_u64(role, fd_rrf);
    PublicKey pk_prime = recv_pk(role, fd_rrf, cs);

    // Sample ρ from D_q, compute fpk = MaulPK(pk', ρ)
    Mpz rho = rng.random_mpz(cs.secretkey_bound());
    QFI h_rho;
    cs.power_of_h(h_rho, rho);
    QFI fpk_elt;
    cs.Cl_G().nucomp(fpk_elt, pk_prime.elt(), h_rho);
    PublicKey fpk(cs, fpk_elt);

    // Forward n_oles and fpk to Sender immediately so it can begin its own
    // precompute (Enc_fpk(b_i)) while we build the noise tables below.
    send_u64(role, fd_sender, n_oles);
    send_pk(role, fd_sender, fpk);
    fprintf(stderr, "[S-RF] forwarded (n_oles, fpk) to Sender\n");

    // ── Precompute R1 rerandomisation noise (under fpk) ───────
    // (h^{r_i}, fpk^{r_i}) — consumed in R1 after AlignEnc.
    std::vector<QFI> rr1_R(n_oles), rr1_E(n_oles);
    for (uint64_t i = 0; i < n_oles; ++i) {
        Mpz ri = rng.random_mpz(cs.secretkey_bound());
        cs.power_of_h(rr1_R[i], ri);
        fpk.exponentiation(cs, rr1_E[i], ri);
    }

    // ── Precompute R2 rerandomisation noise (under pk') ───────
    // (h^{s_i}, pk'^{s_i}) — consumed in R2 after InvAlignEnc.
    std::vector<QFI> rr2_R(n_oles), rr2_E(n_oles);
    for (uint64_t i = 0; i < n_oles; ++i) {
        Mpz si = rng.random_mpz(cs.secretkey_bound());
        cs.power_of_h(rr2_R[i], si);
        pk_prime.exponentiation(cs, rr2_E[i], si);
    }

    // ── Round 1: AlignEnc(c_i'', ρ) + ReRand → Sender ────────
    // Per-iteration cost: 1×nupow (AlignEnc, depends on ci.c1()) + 3×nucomp.
    QFI R_pow_rho, E_aligned, R_out, E_out;
    for (uint64_t i = 0; i < n_oles; ++i) {
        CipherText ci = recv_ct(role, fd_rrf);

        // AlignEnc: re-key from pk' to fpk
        // E_aligned = ci.c2() · ci.c1()^ρ
        cs.Cl_G().nupow(R_pow_rho, ci.c1(), rho);
        if (cs.compact_variant())
            cs.from_Cl_DeltaK_to_Cl_Delta(R_pow_rho);
        cs.Cl_Delta().nucomp(E_aligned, ci.c2(), R_pow_rho);

        // ReRand: compose with precomputed Enc_fpk(0; r_i)
        cs.Cl_G().nucomp(R_out, ci.c1(),   rr1_R[i]);
        cs.Cl_Delta().nucomp(E_out, E_aligned, rr1_E[i]);

        CipherText ci_out(R_out, E_out);
        send_ct(role, fd_sender, ci_out);
    }

    // ── Round 2: InvAlignEnc(ez_i, ρ) + ReRand → R-RF ────────
    // Per-iteration cost: 1×nupow (InvAlignEnc, depends on ez.c1()) +
    // 1×nucompinv + 2×nucomp.
    QFI Ry_pow_rho, E_back, R_final, E_final;
    for (uint64_t i = 0; i < n_oles; ++i) {
        CipherText ez = recv_ct(role, fd_sender);

        // InvAlignEnc: re-key from fpk back to pk'
        // E_back = ez.c2() / ez.c1()^ρ
        cs.Cl_G().nupow(Ry_pow_rho, ez.c1(), rho);
        if (cs.compact_variant())
            cs.from_Cl_DeltaK_to_Cl_Delta(Ry_pow_rho);
        cs.Cl_Delta().nucompinv(E_back, ez.c2(), Ry_pow_rho);

        // ReRand: compose with precomputed Enc_pk'(0; s_i)
        cs.Cl_G().nucomp(R_final, ez.c1(), rr2_R[i]);
        cs.Cl_Delta().nucomp(E_final, E_back,  rr2_E[i]);

        CipherText z_prime(R_final, E_final);
        send_ct(role, fd_rrf, z_prime);
    }

    uint32_t ver = recv_u32(role, fd_rrf);
    send_u32(role, fd_sender, ver);

    close(fd_rrf);
    close(fd_sender);
    fprintf(stderr, "[S-RF] done. verified=%u\n", ver);
    return 0;
}
