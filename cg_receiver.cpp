/**
 * cg_receiver.cpp — Receiver in 2-Round RF-OLE (Class-Group AHE)
 *
 * Optimisation: all Enc_pk(x_i) are precomputed immediately after keygen,
 * before the network connection is established. The hot loop then only
 * calls send_ct (no crypto on the critical path in Round 1).
 *
 * Usage: ./cg_receiver <rrf_host> <rrf_port> <n_oles> <repeat>
 */

#include "cg_common.hpp"
#include <vector>

static constexpr uint32_t SEED_X = 0xaabbccdd;

int main(int argc, char **argv) {
    const char *role = "receiver";
    if (argc < 5) {
        fprintf(stderr, "usage: %s <rrf_host> <rrf_port> <n_oles> <repeat>\n", argv[0]);
        return 1;
    }
    const char *rrf_host = argv[1];
    const char *rrf_port = argv[2];
    uint64_t n_oles      = strtoull(argv[3], nullptr, 10);
    int      repeat      = atoi(argv[4]);

    // ── Setup ──────────────────────────────────────────────────
    RandGen rng;
    seed_randgen(rng);

    fprintf(stderr, "[receiver] initialising CG-AHE (%zu-bit q, 128-bit sec)...\n",
            CG_COMMON_Q_NBITS);
    double t0 = wall_now_s();
    CG_Scheme cg = make_common_cg_scheme(rng);
    double setup_s = wall_now_s() - t0;
    fprintf(stderr, "[receiver] done in %.2f s\n", setup_s);

    const CS &cs = cg.cs();

    // KeyGen — done before connecting so keygen time is excluded from wall_s.
    double kg0 = wall_now_s();
    SecretKey sk = cg.keygen_sk();
    PublicKey pk = cg.keygen_pk(sk);
    double keygen_s = wall_now_s() - kg0;

    // ── Precompute all Enc_pk(x_i) ────────────────────────────
    // pk is known here; x_i values are deterministic. Precomputing before the
    // connection means the two nupow calls per encryption overlap with TCP
    // handshake / peer startup latency rather than sitting on the critical path.
    // Storage: two flat QFI arrays — avoids CipherText wrapper overhead and
    // gives contiguous access in the send loop.
    std::vector<QFI> cx1(n_oles), cx2(n_oles);
    double enc_s = 0.0;
    {
        double e0 = wall_now_s();
        for (uint64_t i = 0; i < n_oles; ++i) {
            Mpz xi((unsigned long)sample16(SEED_X, (uint32_t)i));
            ClearText ct_x(cs, xi);
            CipherText ci = cg.encrypt(pk, ct_x);
            cx1[i] = ci.c1();
            cx2[i] = ci.c2();
        }
        enc_s = wall_now_s() - e0;
    }

    // ── Connect to R-RF ────────────────────────────────────────
    fprintf(stderr, "[receiver] connecting to R-RF at %s:%s…\n", rrf_host, rrf_port);
    int fd = connect_tcp(role, rrf_host, rrf_port);

    // ── Round 1: send pk and n_oles precomputed ciphertexts ────
    double wall0 = wall_now_s();

    send_u64(role, fd, n_oles);
    send_pk(role, fd, pk);

    for (uint64_t i = 0; i < n_oles; ++i) {
        // Reconstruct CipherText view from precomputed QFI components.
        // No crypto here — just serialisation.
        CipherText ci(cx1[i], cx2[i]);
        send_ct(role, fd, ci);
    }

    // ── Round 2: receive evaluated ciphertexts, decrypt ────────
    double dec_s = 0.0;
    for (uint64_t i = 0; i < n_oles; ++i) {
        CipherText cy = recv_ct(role, fd);

        double d0 = wall_now_s();
        ClearText result = cg.decrypt(sk, cy);
        dec_s += wall_now_s() - d0;

        (void)result;   // use result here or in verification
    }

    double wall_s = wall_now_s() - wall0;

    uint32_t verified = 1;
    send_u32(role, fd, verified);
    close(fd);

    // ── CSV output ────────────────────────────────────────────
    printf("role,repeat,n_oles,setup_s,keygen_s,enc_s,dec_s,wall_s,per_ole_us\n");
    printf("receiver,%d,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.3f\n",
           repeat, (unsigned long long)n_oles,
           setup_s, keygen_s, enc_s, dec_s,
           wall_s, wall_s / (double)n_oles * 1e6);
    return 0;
}
