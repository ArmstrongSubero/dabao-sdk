/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ed25519 (RFC 8032 section 5.1, pure) for the Baochip-1x.
 *
 * Point multiplication and addition run on the PKE through the PKE
 * driver's Ed25519 point functions, which take affine points as 32 byte
 * little endian coordinates. Encoding and decoding of points (5.1.2 and
 * 5.1.3, with the square root of 5.1.3 step 2 on the PKE's modular
 * exponent), the clamping of the secret scalar and the reductions mod L
 * are done here: wide values go through the ALU divider, products mod L
 * and mod p through the PKE's modular functions. SHA-512 is the SCE hash
 * engine through the SHA driver. No register is touched here.
 *
 * Nothing here is constant time.
 */

#include "bao/platform.h"
#include "hardware/pke.h"
#include "hardware/alu.h"
#include "hardware/sha.h"
#include "hardware/sign.h"
#include "sign_bn.h"

/* p = 2^255 minus 19 */
static const uint32_t ED_P[8] = {
    0xFFFFFFED, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x7FFFFFFF,
};
/* d = -121665 / 121666 mod p */
static const uint32_t ED_D[8] = {
    0x135978A3, 0x75EB4DCA, 0x4141D8AB, 0x00700A4D,
    0x7779E898, 0x8CC74079, 0x2B6FFE73, 0x52036CEE,
};
/* L = 2^252 + 27742317777372353535851937790883648493, the group order */
static const uint32_t ED_L[8] = {
    0x5CF5D3ED, 0x5812631A, 0xA2F79CD6, 0x14DEF9DE,
    0x00000000, 0x00000000, 0x00000000, 0x10000000,
};
/* 2^((p minus 1) / 4) mod p, a square root of minus 1 */
static const uint32_t ED_SQRTM1[8] = {
    0x4A0EA0B0, 0xC4EE1B27, 0xAD2FE478, 0x2F431806,
    0x3DFBD7A7, 0x2B4D0099, 0x4FC1DF0B, 0x2B832480,
};
/* (p minus 5) / 8, the exponent of 5.1.3 step 2 */
static const uint32_t ED_EXP[8] = {
    0xFFFFFFFD, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x0FFFFFFF,
};
static const uint32_t ED_ONE[8]  = { 1, 0, 0, 0, 0, 0, 0, 0 };
static const uint32_t ED_ZERO[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };

#define TRY(x) do { rc = (x); if (rc != SIGN_OK) goto done; } while (0)
#define HW(x)  (((x) == 0) ? SIGN_OK : SIGN_ERR_HW)

/* out = the little endian number in bytes (4 * words long) mod L, on the
 * ALU divider. out is 8 words. */
static int reduce_l(const uint8_t *bytes, uint32_t words, uint32_t *out)
{
    uint32_t x[SIGN_MAX_WORDS], rw = 0;
    int rc;

    sign_le_to_words(x, bytes, words);
    for (uint32_t i = 0; i < 8u; i++)
        out[i] = 0;
    rc = alu_div(x, words, ED_L, 8, NULL, NULL, out, &rw);
    sign_wipe(x, sizeof(x));
    if (rc != ALU_OK || rw > 8u)
        return SIGN_ERR_HW;
    sign_fix(out, ED_L, 8);
    return SIGN_OK;
}

static void set_identity(pke_ed_point_t *pt)
{
    for (uint32_t i = 0; i < 32u; i++) {
        pt->x[i] = 0;
        pt->y[i] = 0;
    }
    pt->y[0] = 1;
}

static bool is_identity(const pke_ed_point_t *pt)
{
    uint8_t acc = pt->y[0] ^ 1u;
    for (uint32_t i = 0; i < 32u; i++)
        acc |= pt->x[i];
    for (uint32_t i = 1; i < 32u; i++)
        acc |= pt->y[i];
    return acc == 0;
}

/* 5.1.2: y little endian with the low bit of x in bit 255. The engine's
 * coordinates are brought below p first. */
static void encode(const pke_ed_point_t *pt, uint8_t out[32])
{
    uint32_t x[8], y[8];
    sign_le_to_words(x, pt->x, 8);
    sign_le_to_words(y, pt->y, 8);
    sign_fix(x, ED_P, 8);
    sign_fix(y, ED_P, 8);
    sign_words_to_le(out, y, 8);
    out[31] |= (uint8_t)((x[0] & 1u) << 7);
}

/*
 * 5.1.3. SIGN_BAD when y is not below p, when (y^2 minus 1) / (d y^2 + 1)
 * has no square root, or when x is 0 and its sign bit is set. The root is
 * x = u v^3 (u v^7)^((p minus 5) / 8) with u = y^2 minus 1, v = d y^2 + 1,
 * times sqrt(minus 1) when v x^2 = minus u.
 */
static int decode(const uint8_t in[32], pke_ed_point_t *pt)
{
    uint32_t y[8], u[8], v[8], v3[8], t[8], x[8], c[8];
    const uint32_t sign = (uint32_t)in[31] >> 7;
    int rc;

    sign_le_to_words(y, in, 8);
    y[7] &= 0x7FFFFFFFu;
    if (sign_cmp(y, ED_P, 8) >= 0)
        return SIGN_BAD;

    TRY(m_mul(t, y, y, ED_P, 8));
    TRY(m_sub(u, t, ED_ONE, ED_P, 8));
    TRY(m_mul(v, ED_D, t, ED_P, 8));
    TRY(m_add(v, v, ED_ONE, ED_P, 8));

    TRY(m_mul(v3, v, v, ED_P, 8));
    TRY(m_mul(v3, v3, v, ED_P, 8));
    TRY(m_mul(t, v3, v3, ED_P, 8));
    TRY(m_mul(t, t, v, ED_P, 8));
    TRY(m_mul(t, t, u, ED_P, 8));
    TRY(HW(pke_mod_exp(t, 8, ED_EXP, 8, ED_P, 8, c)));
    sign_fix(c, ED_P, 8);
    TRY(m_mul(x, u, v3, ED_P, 8));
    TRY(m_mul(x, x, c, ED_P, 8));

    TRY(m_mul(t, x, x, ED_P, 8));
    TRY(m_mul(t, t, v, ED_P, 8));
    if (sign_cmp(t, u, 8) != 0) {
        TRY(m_sub(c, ED_ZERO, u, ED_P, 8));
        if (sign_cmp(t, c, 8) != 0) {
            rc = SIGN_BAD;
            goto done;
        }
        TRY(m_mul(x, x, ED_SQRTM1, ED_P, 8));
    }
    if (sign_is_zero(x, 8) && sign) {
        rc = SIGN_BAD;
        goto done;
    }
    if ((x[0] & 1u) != sign)
        TRY(m_sub(x, ED_ZERO, x, ED_P, 8));

    sign_words_to_le(pt->x, x, 8);
    sign_words_to_le(pt->y, y, 8);
done:
    return rc;
}

/* out = k B and out = k P for a scalar below L, with 0 and the identity
 * handled here so the engine never sees them. */
static int mul_base(const uint32_t k[8], pke_ed_point_t *out)
{
    uint8_t kb[32];
    int rc;
    if (sign_is_zero(k, 8)) {
        set_identity(out);
        return SIGN_OK;
    }
    sign_words_to_le(kb, k, 8);
    rc = HW(pke_ed25519_point_mul_base(kb, out));
    sign_wipe(kb, sizeof(kb));
    return rc;
}

static int mul_point(const uint32_t k[8], const pke_ed_point_t *p, pke_ed_point_t *out)
{
    uint8_t kb[32];
    int rc;
    if (sign_is_zero(k, 8) || is_identity(p)) {
        set_identity(out);
        return SIGN_OK;
    }
    sign_words_to_le(kb, k, 8);
    rc = HW(pke_ed25519_point_mul(kb, p, out));
    sign_wipe(kb, sizeof(kb));
    return rc;
}

static int add_points(const pke_ed_point_t *a, const pke_ed_point_t *b, pke_ed_point_t *out)
{
    if (is_identity(a)) {
        *out = *b;
        return SIGN_OK;
    }
    if (is_identity(b)) {
        *out = *a;
        return SIGN_OK;
    }
    return HW(pke_ed25519_point_add(a, b, out));
}

/* 5.1.5 steps 1 to 3: h = SHA-512(seed), s = the clamped first half,
 * A = s B encoded. h is left for the caller (its second half is the
 * prefix of 5.1.6). */
static int expand(const uint8_t seed[32], uint8_t h[64], uint8_t pub[32])
{
    pke_ed_point_t A;
    uint8_t s[32];
    int rc;

    TRY(HW(sha512_hash(seed, 32, h)));
    for (uint32_t i = 0; i < 32u; i++)
        s[i] = h[i];
    s[0]  &= 248u;
    s[31] &= 127u;
    s[31] |= 64u;
    TRY(HW(pke_ed25519_point_mul_base(s, &A)));
    encode(&A, pub);
done:
    sign_wipe(s, sizeof(s));
    sign_wipe(&A, sizeof(A));
    return rc;
}

/* out = SHA-512(a || b || msg) mod L; a or b may be NULL */
static int hash_mod_l(const uint8_t *a, const uint8_t *b, const uint8_t *msg,
                      uint32_t msg_len, uint32_t out[8])
{
    sha512_ctx_t c;
    uint8_t d[64];
    int rc = sha512_stream_init(&c);
    if (rc == SHA_OK && a != NULL)
        rc = sha512_stream_update(&c, a, 32);
    if (rc == SHA_OK && b != NULL)
        rc = sha512_stream_update(&c, b, 32);
    if (rc == SHA_OK)
        rc = sha512_stream_update(&c, msg, msg_len);
    if (rc == SHA_OK)
        rc = sha512_stream_final(&c, d);
    rc = (rc == SHA_OK) ? reduce_l(d, 16, out) : SIGN_ERR_HW;
    sign_wipe(&c, sizeof(c));
    sign_wipe(d, sizeof(d));
    return rc;
}

int ed25519_public_key(const uint8_t seed[32], uint8_t pub[32])
{
    uint8_t h[64];
    int rc;

    if (seed == NULL || pub == NULL)
        return SIGN_ERR_ARG;
    rc = expand(seed, h, pub);
    sign_wipe(h, sizeof(h));
    return wipe_engine(rc);
}

/*
 * 5.1.6: r = SHA-512(prefix || M) mod L, R = r B, k = SHA-512(R || A || M)
 * mod L, S = (r + k s) mod L, signature R || S.
 */
int ed25519_sign(const uint8_t seed[32], const uint8_t *msg, uint32_t msg_len,
                 uint8_t sig[64])
{
    uint8_t h[64], pub[32], renc[32];
    uint32_t s[8], r[8], k[8], S[8];
    pke_ed_point_t R;
    int rc;

    if (seed == NULL || sig == NULL || (msg == NULL && msg_len != 0))
        return SIGN_ERR_ARG;

    /* sig is written only at the end, so msg may share its buffer */
    TRY(expand(seed, h, pub));
    TRY(hash_mod_l(h + 32, NULL, msg, msg_len, r));
    TRY(mul_base(r, &R));
    encode(&R, renc);
    TRY(hash_mod_l(renc, pub, msg, msg_len, k));

    /* the clamped scalar again, reduced mod L for the PKE's modular multiply */
    h[0]  &= 248u;
    h[31] &= 127u;
    h[31] |= 64u;
    TRY(reduce_l(h, 8, s));
    TRY(m_mul(S, k, s, ED_L, 8));
    TRY(m_add(S, S, r, ED_L, 8));
    for (uint32_t i = 0; i < 32u; i++)
        sig[i] = renc[i];
    sign_words_to_le(sig + 32, S, 8);
done:
    sign_wipe(h, sizeof(h));
    sign_wipe(s, sizeof(s));
    sign_wipe(r, sizeof(r));
    sign_wipe(S, sizeof(S));
    sign_wipe(&R, sizeof(R));
    rc = wipe_engine(rc);
    if (rc != SIGN_OK)
        sign_wipe(sig, 64);
    return rc;
}

/*
 * 5.1.7: S below L, A and R decode, k = SHA-512(R || A || M) mod L with R
 * and A as given, and [S]B = R + [k]A compared as encodings (the encoding
 * of a point below p is unique).
 */
int ed25519_verify(const uint8_t pub[32], const uint8_t *msg, uint32_t msg_len,
                   const uint8_t sig[64])
{
    uint32_t S[8], k[8];
    pke_ed_point_t A, R, P, Q;
    uint8_t e1[32], e2[32];
    int rc;

    if (pub == NULL || sig == NULL || (msg == NULL && msg_len != 0))
        return SIGN_ERR_ARG;

    sign_le_to_words(S, sig + 32, 8);
    if (sign_cmp(S, ED_L, 8) >= 0)
        return SIGN_BAD;

    rc = decode(pub, &A);
    if (rc == SIGN_BAD)
        return SIGN_ERR_KEY;
    if (rc != SIGN_OK)
        return rc;
    TRY(decode(sig, &R));
    TRY(hash_mod_l(sig, pub, msg, msg_len, k));

    TRY(mul_base(S, &P));
    TRY(mul_point(k, &A, &Q));
    TRY(add_points(&R, &Q, &Q));
    encode(&P, e1);
    encode(&Q, e2);
    rc = sign_same(e1, e2, 32) ? SIGN_OK : SIGN_BAD;
done:
    return rc;
}
