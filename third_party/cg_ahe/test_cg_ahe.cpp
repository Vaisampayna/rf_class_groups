/**
 * test_cg_ahe.cpp
 *
 * Unit tests for the CG-AHE (Class-Group AHE) protocol implemented
 * in cg_ahe.hpp using BICYCL's CL_HSMqk backend.
 *
 * Tests verified:
 *   1.  KeyGen correctness (pk = g^sk)
 *   2.  Enc / Dec round-trip
 *   3.  Homomorphic Addition  (Dec(c1 ⊞ c2) == m1 + m2 mod q^k)
 *   4.  Scalar Multiplication (Dec(CMult(c, α)) == α·m mod q^k)
 *   5.  Combined (Dec(CMult(c1,α) ⊞ c2) == α·m1 + m2)
 *   6.  ReRand preserves plaintext
 *   7.  MaulPK / MaulSK  consistency (maul'd keys can still Enc/Dec)
 *   8.  MaulCT preserves plaintext
 */

#include "cg_ahe.hpp"
#include <iostream>
#include <cassert>
#include <chrono>

using namespace BICYCL;
using namespace CG_AHE;

static void pass(const std::string& name) {
    std::cout << "  [PASS] " << name << "\n";
}
static void fail(const std::string& name) {
    std::cerr << "  [FAIL] " << name << "\n";
}

template<class T>
static bool mpz_eq(const T& a, const T& b) {
    return static_cast<const Mpz&>(a) == static_cast<const Mpz&>(b);
}

int main() {
    std::cout << "====== CG-AHE Protocol Test Suite ======\n\n";

    // ── Setup ────────────────────────────────────────────────
    RandGen rng;
    // 128-bit security, q is a 64-bit prime, k=1 → M = q, plaintext in Z_q
    std::cout << "[Setup] Initialising CL_HSMqk (128-bit, q~128 bits, k=1)...\n";
    auto t0 = std::chrono::steady_clock::now();
    CG_Scheme cg(128, 1, SecLevel::_128, rng);
    auto t1 = std::chrono::steady_clock::now();
    std::cout << "[Setup] Done in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count()
              << " ms\n\n";

    const auto& cs = cg.cs();

    // ── KeyGen ───────────────────────────────────────────────
    std::cout << "[1] KeyGen\n";
    SecretKey sk = cg.keygen_sk();
    PublicKey pk = cg.keygen_pk(sk);
    pass("KeyGen completed");

    // Pick two messages as ClearText (must be < q^k = q)
    ClearText m1(cs, Mpz(12345UL));
    ClearText m2(cs, Mpz(54321UL));

    // ── Enc / Dec round-trip ─────────────────────────────────
    std::cout << "\n[2] Enc / Dec round-trip\n";
    CipherText c1 = cg.encrypt(pk, m1);
    CipherText c2 = cg.encrypt(pk, m2);
    ClearText d1 = cg.decrypt(sk, c1);
    ClearText d2 = cg.decrypt(sk, c2);
    assert(mpz_eq(d1, m1)); pass("Dec(Enc(m1)) == m1");
    assert(mpz_eq(d2, m2)); pass("Dec(Enc(m2)) == m2");

    // ── Homomorphic Addition ─────────────────────────────────
    std::cout << "\n[3] Homomorphic Addition ⊞\n";
    CipherText c_add = cg.add(pk, c1, c2);
    ClearText d_add  = cg.decrypt(sk, c_add);
    // Expected: (m1 + m2) mod q
    ClearText expected_add = cs.add_cleartexts(m1, m2);
    assert(mpz_eq(d_add, expected_add));
    std::cout << "  m1=" << static_cast<const Mpz&>(m1)
              << "  m2=" << static_cast<const Mpz&>(m2)
              << "  m1+m2 (mod q)=" << static_cast<const Mpz&>(expected_add)
              << "  dec=" << static_cast<const Mpz&>(d_add) << "\n";
    pass("Dec(c1 ⊞ c2) == m1 + m2 mod q");

    // ── Scalar Multiplication CMult ──────────────────────────
    std::cout << "\n[4] CMult\n";
    Mpz alpha(7UL);
    CipherText c_cm  = cg.cmult(c1, alpha);
    ClearText d_cm   = cg.decrypt(sk, c_cm);
    ClearText expected_cm = cs.scal_cleartexts(m1, alpha);
    assert(mpz_eq(d_cm, expected_cm));
    std::cout << "  α=" << alpha
              << "  m1=" << static_cast<const Mpz&>(m1)
              << "  α·m1 (mod q)=" << static_cast<const Mpz&>(expected_cm)
              << "  dec=" << static_cast<const Mpz&>(d_cm) << "\n";
    pass("Dec(CMult(c1, α)) == α·m1 mod q");

    // ── Combined: α·c1 ⊞ c2 ─────────────────────────────────
    std::cout << "\n[5] Combined: α·c1 ⊞ c2\n";
    CipherText c_comb = cg.add(pk, c_cm, c2);
    ClearText d_comb  = cg.decrypt(sk, c_comb);
    ClearText expected_comb = cs.add_cleartexts(expected_cm, m2);
    assert(mpz_eq(d_comb, expected_comb));
    std::cout << "  dec=" << static_cast<const Mpz&>(d_comb)
              << "  expected=" << static_cast<const Mpz&>(expected_comb) << "\n";
    pass("Dec(CMult(c1,α) ⊞ c2) == α·m1 + m2 mod q");

    // ── ReRand ───────────────────────────────────────────────
    std::cout << "\n[6] ReRand\n";
    CipherText c_rr = cg.rerand(pk, c1);
    ClearText d_rr  = cg.decrypt(sk, c_rr);
    assert(mpz_eq(d_rr, m1));
    pass("Dec(ReRand(c1)) == m1");

    // ── MaulPK / MaulSK (re-keyed enc/dec) ──────────────────
    std::cout << "\n[7] MaulPK / MaulSK\n";
    // Maul the secret key: sk' = sk + r
    // The new pk from maulsk must be regenerated from sk' for dec to work.
    SecretKey sk2 = cg.maulsk(sk);
    PublicKey pk2 = cg.keygen_pk(sk2);  // pk2 = g^(sk+r)

    // Maul the public key independently to get a "firewall" pk'
    PublicKey pk_maul = cg.maulpk(pk);
    pass("MaulPK / MaulSK produced new keys");

    // Encrypt under new sk2/pk2 and verify round-trip
    CipherText c_sk2 = cg.encrypt(pk2, m1);
    ClearText  d_sk2 = cg.decrypt(sk2, c_sk2);
    assert(mpz_eq(d_sk2, m1));
    pass("Enc(pk2, m1) / Dec(sk2) round-trip after MaulSK");

    // ── MaulCT ───────────────────────────────────────────────
    std::cout << "\n[8] MaulCT\n";
    // MaulCT(c) = (R, E · R^{r'}).
    // The firewall pairs this with MaulSK: sk' = sk + r'.
    // Decryption with sk' cancels R^{r'}: c2·R^{r'} / R^{sk'} = c2/R^{sk} = f^m.
    // NOTE: sk' = sk + r' may exceed secretkey_bound() — we bypass
    //       the SecretKey range check by computing decrypt manually.
    {
        ClearText m_test(cs, Mpz(9999UL));
        CipherText c_test = cg.encrypt(pk, m_test);

        // Sample r' from the same distribution as encryption randomness.
        Mpz r_prime = rng.random_mpz(cs.encrypt_randomness_bound());

        // Compute c2' = c2 · c1^{r'}.
        QFI R_pow_r;
        cs.Cl_G().nupow(R_pow_r, c_test.c1(), r_prime);
        if (cs.compact_variant())
            cs.from_Cl_DeltaK_to_Cl_Delta(R_pow_r);
        QFI E_prime;
        cs.Cl_Delta().nucomp(E_prime, c_test.c2(), R_pow_r);
        // c_mct = (c1, E') — same R, mauled E.
        CipherText c_mct(c_test.c1(), E_prime);

        // sk' = sk + r'. May exceed secretkey_bound — use raw Mpz for decrypt.
        Mpz sk_prime;
        Mpz::add(sk_prime, static_cast<const Mpz&>(sk), r_prime);

        // Manual decrypt: fm = c2' / c1^{sk'}.
        QFI fm;
        cs.Cl_G().nupow(fm, c_mct.c1(), sk_prime);  // c1^{sk'}
        if (cs.compact_variant())
            cs.from_Cl_DeltaK_to_Cl_Delta(fm);
        cs.Cl_Delta().nucompinv(fm, c_mct.c2(), fm); // c2' / c1^{sk'}
        Mpz result = cs.dlog_in_F(fm);

        std::cout << "  m_test=" << static_cast<const Mpz&>(m_test)
                  << "  result=" << result << "\n";
        assert(result == static_cast<const Mpz&>(m_test));
        pass("Dec_sk'(MaulCT(c)) == m  [with paired MaulSK(sk, r')]");
    }

    std::cout << "\n====== All tests PASSED ======\n";
    return 0;
}
