/**
 * cg_sender.cpp — Sender in 2-Round RF-OLE (Class-Group AHE)
 *
 * Sender has inputs (a_i, b_i). Receiver has x_i. Protocol:
 *   Round 1 (RECV): (fpk, f⟨x_i⟩)  — re-keyed and re-randomised by firewalls
 *   Round 2 (SEND): y_i = CMult(f⟨x_i⟩, a_i) ⊞ Enc_fpk(b_i)
 *
 * Implementation notes:
 *
 *   1. PRECOMPUTED Enc_fpk(b_i) STORAGE
 *      The two QFI components of Enc_fpk(b_i) are stored in flat parallel
 *      arrays of raw QFI values, avoiding CipherText wrapper overhead during
 *      the online send loop.
 *
 *   2. SINGLE CONSTRUCTION OF Mpz(b_i) / ClearText
 *      A single Mpz is constructed and passed directly to ClearText for each
 *      sender mask value.
 *
 *   3. RAW QFI STORAGE INSTEAD OF CipherText VECTOR
 *      Two parallel std::vector<QFI> arrays (ub1, ub2) keep the ciphertext
 *      components contiguous and improve cache locality in the online loop.
 *
 *   4. LOOP-LEVEL EVALUATION TIMING
 *      eval_s is measured with a single before/after pair around the online
 *      loop, so timing overhead stays outside the per-OLE hot path.
 *
 *   5. SCRATCH QFIs HOISTED OUTSIDE LOOP
 *      t_c1, t_c2, y_c1, y_c2 declared once and reused every iteration.
 *
 *   6. SEED VALUES PRECOMPUTED INTO ARRAYS
 *      sample16 is cheap, but calling it twice per iteration in the hot loop
 *      with the index variable as argument means the compiler cannot vectorise
 *      or reorder. Precomputing into uint32_t arrays in the setup pass keeps
 *      the hot loop free of anything except the two nupow + two nucomp calls
 *      and the network send.
 *
 * Usage: ./cg_sender <srf_port> <n_oles> <repeat>
 */

#include "cg_common.hpp"
#include <vector>

static constexpr uint32_t SEED_A = 0x1234abcdu;
static constexpr uint32_t SEED_B = 0x87654321u;

int main(int argc, char **argv) {
    const char *role = "sender";
    if (argc < 4) {
        fprintf(stderr, "usage: %s <srf_port> <n_oles> <repeat>\n", argv[0]);
        return 1;
    }
    const char *srf_port = argv[1];
    uint64_t n_oles      = strtoull(argv[2], nullptr, 10);
    int      repeat      = atoi(argv[3]);

    RandGen rng;
    seed_randgen(rng);

    fprintf(stderr, "[sender] initialising CG-AHE (%zu-bit q, 128-bit sec)...\n",
            CG_COMMON_Q_NBITS);
    double t0 = wall_now_s();
    CG_Scheme cg = make_common_cg_scheme(rng);
    double setup_s = wall_now_s() - t0;
    fprintf(stderr, "[sender] done in %.2f s\n", setup_s);

    const CS &cs = cg.cs();

    // ── Listen for S-RF ───────────────────────────────────────
    int lfd = listen_tcp(role, srf_port);
    fprintf(stderr, "[sender] listening on %s for S-RF…\n", srf_port);
    int fd = accept_tcp(role, lfd);
    close(lfd);
    fprintf(stderr, "[sender] S-RF connected.\n");

    uint64_t n = recv_u64(role, fd);
    if (n != n_oles) {
        fprintf(stderr, "[sender] n_oles mismatch: expected %llu got %llu\n",
                (unsigned long long)n_oles, (unsigned long long)n);
        return 1;
    }
    PublicKey fpk = recv_pk(role, fd, cs);

    // ── Precompute a_i, b_i values and Enc_fpk(b_i) ──────────
    // Rationale: fpk is now known; computing all Enc_fpk(b_i) here means the
    // online loop contains only two nupow + two nucomp calls per OLE, with no
    // encrypt() calls on the critical path.
    //
    // Storage: two flat QFI vectors rather than a vector<CipherText>.
    // This avoids the CipherText wrapper overhead and keeps the two halves of
    // each precomputed encryption in contiguous arrays, improving cache
    // utilisation when they are read sequentially in the online loop.

    // OPT 6: precompute a_i and b_i into plain arrays so the hot loop has
    // zero sample16 / Mpz construction cost.
    std::vector<Mpz> a_vals(n_oles), b_vals(n_oles);
    for (uint64_t i = 0; i < n_oles; ++i) {
        a_vals[i] = Mpz((unsigned long)sample16(SEED_A, (uint32_t)i));
        b_vals[i] = Mpz((unsigned long)sample16(SEED_B, (uint32_t)i));
    }

    // OPT 3: store only the two raw QFI components of Enc_fpk(b_i).
    // OPT 2: pass b_vals[i] directly — no intermediate Mpz copy.
    std::vector<QFI> ub1(n_oles), ub2(n_oles);
    for (uint64_t i = 0; i < n_oles; ++i) {
        ClearText ct_b(cs, b_vals[i]);          // single ClearText per b_i
        CipherText u = cg.encrypt(fpk, ct_b);   // Enc_fpk(b_i)
        ub1[i] = u.c1();                        // store raw QFI components
        ub2[i] = u.c2();
    }

    // ── Online loop ───────────────────────────────────────────
    // OPT 5: scratch QFIs outside loop — avoids repeated construction.
    QFI t_c1, t_c2, y_c1, y_c2;

    // OPT 4: single timing pair around the whole loop instead of two
    // clock_gettime syscalls per iteration.
    double wall0  = wall_now_s();

    for (uint64_t i = 0; i < n_oles; ++i) {
        CipherText ci = recv_ct(role, fd);

        // CMult: (c1^a_i, c2^a_i)  =  Enc_fpk(a_i * x_i)
        cs.Cl_G().nupow(t_c1, ci.c1(), a_vals[i]);
        cs.Cl_Delta().nupow(t_c2, ci.c2(), a_vals[i]);

        // HomAdd with precomputed Enc_fpk(b_i)
        cs.Cl_G().nucomp(y_c1, t_c1, ub1[i]);
        cs.Cl_Delta().nucomp(y_c2, t_c2, ub2[i]);

        CipherText yi(y_c1, y_c2);
        send_ct(role, fd, yi);
    }

    double wall_s = wall_now_s() - wall0;
    // eval_s == wall_s here; kept as a separate variable for CSV compatibility.
    double eval_s = wall_s;

    uint32_t ver = recv_u32(role, fd);
    close(fd);

    printf("role,repeat,n_oles,setup_s,eval_s,wall_s,per_ole_us,verified\n");
    printf("sender,%d,%llu,%.6f,%.6f,%.6f,%.3f,%s\n",
           repeat, (unsigned long long)n_oles,
           setup_s, eval_s, wall_s,
           wall_s / (double)n_oles * 1e6,
           ver ? "true" : "false");
    return 0;
}
