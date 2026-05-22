/**
 * CG-AHE: Class-Group Additively Homomorphic Encryption
 * Based on the CL15 framework (Castagnos-Laguillaumie 2015).
 *
 * Implemented using BICYCL (CL_HSMqk) with 128-bit security.
 *
 * Operations provided:
 *   CG::KeyGen    – Sample sk ← D_q, pk = g^sk
 *   CG::Enc       – Encrypt m ∈ Z_p as (R=g^r, E=f^m · pk^r)
 *   CG::Dec       – Decrypt (R,E) → m via DLOG in F
 *   CG::Add       – Homomorphic addition (R1R2, E1E2)
 *   CG::CMult     – Scalar multiplication (R^α, E^α)
 *   CG::ReRand    – Re-randomise ciphertext with fresh encryption of 0
 *   CG::MaulPK   – Maul public key (adds fresh random element)
 *   CG::MaulSK   – Maul secret key (sk' = sk + r)
 *   CG::MaulCT   – Maul ciphertext (E' = E · R^r')
 */
#pragma once

#include "bicycl.hpp"   // BICYCL header-only library
#include <stdexcept>
#include <iostream>
#include <cassert>

namespace CG_AHE {

using namespace BICYCL;

// ────────────────────────────────────────────────────────────
//  Type aliases for readability
// ────────────────────────────────────────────────────────────
using CS         = CL_HSMqk;
using SecretKey  = CS::SecretKey;
using PublicKey  = CS::PublicKey;
using ClearText  = CS::ClearText;
using CipherText = CS::CipherText;

// ────────────────────────────────────────────────────────────
//  CG_Scheme: wraps the BICYCL CL_HSMqk cryptosystem with the
//  protocol operations requested.
// ────────────────────────────────────────────────────────────
class CG_Scheme {
public:
    /**
     * Constructor – sets up the class-group cryptosystem.
     *
     * @param q_nbits  Bit-size of the plaintext modulus q.
     *                 Use 16 for p=65537 (matching prior work).
     * @param k        Exponent in M = q^k (use k=1 for standard HSM).
     * @param seclevel Security level (128, 192, or 256 bits).
     * @param randgen  BICYCL random generator.
     */
    CG_Scheme(size_t q_nbits, size_t k, SecLevel seclevel, RandGen& randgen)
        : cs_(q_nbits, k, seclevel, randgen),
          randgen_(randgen) {}

    /**
     * Constructor with separate public-parameter and runtime randomness.
     * The class-group public parameters must be identical for all parties,
     * while encryption/key randomness must remain local to the process.
     */
    CG_Scheme(size_t q_nbits, size_t k, SecLevel seclevel,
              RandGen& public_param_randgen, RandGen& runtime_randgen)
        : cs_(q_nbits, k, seclevel, public_param_randgen),
          randgen_(runtime_randgen) {}

    // ── Accessors ──────────────────────────────────────────
    const CS& cs() const { return cs_; }

    // ────────────────────────────────────────────────────────
    //  KeyGen(pp_CG):
    //    1. sk ← D_q
    //    2. pk := g^sk
    //    3. Output (sk, pk)
    // ────────────────────────────────────────────────────────
    SecretKey keygen_sk() {
        return cs_.keygen(randgen_);
    }

    PublicKey keygen_pk(const SecretKey& sk) {
        return cs_.keygen(sk);
    }

    // ────────────────────────────────────────────────────────
    //  Enc_pk(m) for m ∈ Z_p:
    //    1. r ← D_q
    //    2. R = g^r,  E = f^m · pk^r
    //    3. Output c = (R, E)
    //
    //  BICYCL's encrypt() natively computes this.
    // ────────────────────────────────────────────────────────
    CipherText encrypt(const PublicKey& pk, const ClearText& m) {
        return cs_.encrypt(pk, m, randgen_);
    }

    // Encrypt with explicit randomness r (for ReRand, MaulCT, etc.)
    CipherText encrypt(const PublicKey& pk, const ClearText& m, const Mpz& r) {
        return cs_.encrypt(pk, m, r);
    }

    // ────────────────────────────────────────────────────────
    //  Dec_sk(c):
    //    1. M = E · (R^sk)^{-1}
    //    2. m = DLOG_F(M)
    //    3. Output m
    // ────────────────────────────────────────────────────────
    ClearText decrypt(const SecretKey& sk, const CipherText& c) {
        return cs_.decrypt(sk, c);
    }

    // ────────────────────────────────────────────────────────
    //  Homomorphic Addition ⊞:
    //    (R1,E1) ⊞ (R2,E2) = (R1·R2, E1·E2)
    // ────────────────────────────────────────────────────────
    CipherText add(const PublicKey& pk,
                   const CipherText& c1,
                   const CipherText& c2) {
        // Pure group composition with no rerandomization.  The first component
        // lives in Cl_G, while the second lives in Cl_Delta; using Cl_Delta for
        // both only works accidentally when compact_variant=false.
        (void)pk;
        QFI r1, r2;
        cs_.Cl_G().nucomp(r1, c1.c1(), c2.c1());
        cs_.Cl_Delta().nucomp(r2, c1.c2(), c2.c2());
        return CipherText(r1, r2);
    }

    // ────────────────────────────────────────────────────────
    //  Scalar Multiplication CMult:
    //    CMult((R,E), α) = (R^α, E^α)  for α ∈ Z_p
    // ────────────────────────────────────────────────────────
    CipherText cmult(const CipherText& c, const Mpz& alpha) {
        QFI r1, r2;
        cs_.Cl_G().nupow(r1, c.c1(), alpha);
        cs_.Cl_Delta().nupow(r2, c.c2(), alpha);
        return CipherText(r1, r2);
    }

    // ────────────────────────────────────────────────────────
    //  ReRand_pk(c) for c = (R, E):
    //    1. r ← D_q
    //    2. c0 = Enc_pk(0) = (g^r, pk^r)       [uses BICYCL internally]
    //    3. Output c ⊞ c0
    // ────────────────────────────────────────────────────────
    CipherText rerand(const PublicKey& pk, const CipherText& c) {
        ClearText zero(cs_, Mpz(0UL));
        CipherText c0 = encrypt(pk, zero);
        return add(pk, c, c0);
    }

    // ────────────────────────────────────────────────────────
    //  MaulPK(pk):
    //    1. r ← D_q
    //    2. α = g^r
    //    3. pk' := pk · α     (i.e. g^sk · g^r = g^(sk+r))
    // ────────────────────────────────────────────────────────
    PublicKey maulpk(const PublicKey& pk) {
        // Sample fresh r and compute g^r
        Mpz r = randgen_.random_mpz(cs_.secretkey_bound());
        QFI alpha;
        cs_.power_of_h(alpha, r);
        // pk' = pk · α  in the class group of Cl_G
        QFI new_pk_elt;
        cs_.Cl_G().nucomp(new_pk_elt, pk.elt(), alpha);
        return PublicKey(cs_, new_pk_elt);
    }

    // ────────────────────────────────────────────────────────
    //  MaulSK(sk):
    //    1. r ← D_q
    //    2. sk' := sk + r
    // ────────────────────────────────────────────────────────
    SecretKey maulsk(const SecretKey& sk) {
        Mpz r = randgen_.random_mpz(cs_.secretkey_bound());
        Mpz new_sk_val;
        Mpz::add(new_sk_val, static_cast<const Mpz&>(sk), r);
        // Keep the result within the allowed secret key range.
        Mpz::mod(new_sk_val, new_sk_val, cs_.secretkey_bound());
        return SecretKey(cs_, new_sk_val);
    }

    // ────────────────────────────────────────────────────────
    //  MaulCT(c, τ) for c = (R, E):
    //    1. r' ← D_q
    //    2. E' = E · R^{r'}
    //    3. Output c' = (R, E')
    //
    //  Note: τ is not used in the computation per the spec;
    //  it only conditions the sampling of r'.
    // ────────────────────────────────────────────────────────
    CipherText maulct(const CipherText& c) {
        Mpz r_prime = randgen_.random_mpz(cs_.secretkey_bound());
        // Compute R^{r'} from the c1 group, converting only when the compact
        // variant stores c1 in Cl_DeltaK and c2 in Cl_Delta.
        QFI R_pow_r;
        cs_.Cl_G().nupow(R_pow_r, c.c1(), r_prime);
        if (cs_.compact_variant())
            cs_.from_Cl_DeltaK_to_Cl_Delta(R_pow_r);
        // E' = E · R^{r'}
        QFI E_prime;
        cs_.Cl_Delta().nucomp(E_prime, c.c2(), R_pow_r);
        return CipherText(c.c1(), E_prime);
    }

    // ── Helper: fresh randomness scalar ─────────────────────
    Mpz fresh_randomness() {
        return randgen_.random_mpz(cs_.encrypt_randomness_bound());
    }

private:
    CS       cs_;
    RandGen& randgen_;
};

} // namespace CG_AHE
