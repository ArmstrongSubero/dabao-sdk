/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * ECDSA for the Baochip-1x on P-256, secp256k1 and P-384, with the RFC 6979
 * deterministic nonce.
 *
 * Written from SEC 1 version 2 section 4.1 (signing and verifying), RFC
 * 6979 section 3.2 (the nonce from HMAC_DRBG) and the curve parameters of
 * SEC 2 version 2 (secp256k1, secp256r1, secp384r1), on top of the PKE
 * driver (points and modular arithmetic) and the SHA driver (hashes and
 * HMAC). No register is touched here.
 *
 * The three curves have cofactor 1 and orders n of exactly 256 and 384
 * bits, so qlen is a whole number of bytes and bits2int of a string is its
 * first qlen / 8 bytes. p is above n and below 2n on all three, so a
 * coordinate reduces mod n with one subtraction.
 *
 * Nothing here is constant time.
 */

#include "bao/platform.h"
#include "hardware/pke.h"
#include "hardware/sha.h"
#include "hardware/sign.h"
#include "sign_bn.h"

/* Curve parameters (SEC 2 version 2), little endian words. G comes from
 * the PKE driver (pke_ecc_base_point), whose tables agree with SEC 2. */

static const uint32_t P256_P[8] = {
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
    0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF,
};
static const uint32_t P256_B[8] = {
    0x27D2604B, 0x3BCE3C3E, 0xCC53B0F6, 0x651D06B0,
    0x769886BC, 0xB3EBBD55, 0xAA3A93E7, 0x5AC635D8,
};
static const uint32_t P256_N[8] = {
    0xFC632551, 0xF3B9CAC2, 0xA7179E84, 0xBCE6FAAD,
    0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF,
};

static const uint32_t K256_P[8] = {
    0xFFFFFC2F, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
};
static const uint32_t K256_B[8] = {
    0x00000007, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000,
};
static const uint32_t K256_N[8] = {
    0xD0364141, 0xBFD25E8C, 0xAF48A03B, 0xBAAEDCE6,
    0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
};

static const uint32_t P384_P[12] = {
    0xFFFFFFFF, 0x00000000, 0x00000000, 0xFFFFFFFF,
    0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
};
static const uint32_t P384_B[12] = {
    0xD3EC2AEF, 0x2A85C8ED, 0x8A2ED19D, 0xC656398D,
    0x5013875A, 0x0314088F, 0xFE814112, 0x181D9C6E,
    0xE3F82D19, 0x988E056B, 0xE23EE7E4, 0xB3312FA7,
};
static const uint32_t P384_N[12] = {
    0xCCC52973, 0xECEC196A, 0x48B0A77A, 0x581A0DB2,
    0xF4372DDF, 0xC7634D81, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
};

typedef struct {
    pke_curve_t     id;
    uint8_t         words;      /* words per coordinate, scalar, r and s */
    uint8_t         a_minus3;   /* 1 when a = p minus 3, 0 when a = 0     */
    const uint32_t *p;
    const uint32_t *b;
    const uint32_t *n;
} ec_t;

static const ec_t s_ec[] = {
    { PKE_SECP256R1, 8,  1, P256_P, P256_B, P256_N },
    { PKE_SECP256K1, 8,  0, K256_P, K256_B, K256_N },
    { PKE_SECP384R1, 12, 1, P384_P, P384_B, P384_N },
};

#define EC_MAX_WORDS   12u
#define EC_MAX_BYTES   48u
#define HASH_MAX_BYTES 64u
/* RFC 6979 asks for a new candidate until one works; the chance that even
 * the second is needed is about 2^-128, so this bound is never reached by
 * a working engine. */
#define RFC6979_TRIES  16u

static const ec_t *ec_find(pke_curve_t id)
{
    for (uint32_t i = 0; i < sizeof(s_ec) / sizeof(s_ec[0]); i++) {
        if (s_ec[i].id == id)
            return &s_ec[i];
    }
    return NULL;
}

uint32_t ecdsa_key_size(pke_curve_t curve)
{
    const ec_t *ec = ec_find(curve);
    return ec ? 4u * ec->words : 0;
}

#define TRY(x) do { rc = (x); if (rc != SIGN_OK) goto done; } while (0)

/*
 * Public key check (SEC 1 3.2.2.1 for cofactor 1): x and y below p, not
 * the all zero encoding, and y^2 = x^3 + a x + b mod p. With cofactor 1
 * every point on the curve other than infinity has order n, so n Q need
 * not be checked.
 */
static int check_point(const ec_t *ec, const uint32_t *q)
{
    const uint32_t w = ec->words;
    const uint32_t *x = q, *y = q + w;
    uint32_t lhs[EC_MAX_WORDS], rhs[EC_MAX_WORDS], t[EC_MAX_WORDS];
    int rc;

    if (sign_cmp(x, ec->p, w) >= 0 || sign_cmp(y, ec->p, w) >= 0)
        return SIGN_ERR_KEY;
    if (sign_is_zero(x, w) && sign_is_zero(y, w))
        return SIGN_ERR_KEY;

    TRY(m_mul(lhs, y, y, ec->p, w));
    TRY(m_mul(t, x, x, ec->p, w));
    TRY(m_mul(rhs, t, x, ec->p, w));
    TRY(m_add(rhs, rhs, ec->b, ec->p, w));
    if (ec->a_minus3) {
        TRY(m_add(t, x, x, ec->p, w));
        TRY(m_add(t, t, x, ec->p, w));
        TRY(m_sub(rhs, rhs, t, ec->p, w));
    }
    rc = sign_cmp(lhs, rhs, w) == 0 ? SIGN_OK : SIGN_ERR_KEY;
done:
    return rc;
}

/*
 * bits2octets of RFC 6979 2.3.4 as a number: bits2int(h) (the first qlen
 * bits of the hash, or all of it when shorter) reduced mod n. It is also
 * the e of ECDSA (SEC 1 4.1.3 step 5), which is the same value.
 */
static void hash_to_e(const ec_t *ec, const uint8_t *hash, uint32_t hash_len, uint32_t *e)
{
    const uint32_t qb = 4u * ec->words;
    uint8_t buf[EC_MAX_BYTES];
    uint32_t i;

    if (hash_len >= qb) {
        for (i = 0; i < qb; i++)
            buf[i] = hash[i];
    } else {
        for (i = 0; i < qb - hash_len; i++)
            buf[i] = 0;
        for (i = 0; i < hash_len; i++)
            buf[qb - hash_len + i] = hash[i];
    }
    sign_be_to_words(e, buf, ec->words);
    sign_fix(e, ec->n, ec->words);
}

static int hmac_h(sign_hash_t h, const uint8_t *key, const uint8_t *msg,
                  uint32_t len, uint8_t *mac)
{
    int rc = (h == SIGN_SHA256) ? hmac_sha256(key, 32, msg, len, mac)
                                : hmac_sha512(key, 64, msg, len, mac);
    return rc == SHA_OK ? SIGN_OK : SIGN_ERR_HW;
}

static void copy_bytes(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        dst[i] = src[i];
}

/* The RFC 6979 HMAC_DRBG state of section 3.2 steps b to h */
typedef struct {
    uint8_t K[HASH_MAX_BYTES];
    uint8_t V[HASH_MAX_BYTES];
    uint8_t mac[HASH_MAX_BYTES];
    /* V, a separator byte, int2octets(x) and bits2octets(h1) */
    uint8_t m[HASH_MAX_BYTES + 1u + 2u * EC_MAX_BYTES];
    uint8_t T[EC_MAX_BYTES];
} drbg_t;

/* V = HMAC_K(V) */
static int drbg_next_v(drbg_t *g, sign_hash_t h, uint32_t hlen)
{
    int rc = hmac_h(h, g->K, g->V, hlen, g->mac);
    if (rc == SIGN_OK)
        copy_bytes(g->V, g->mac, hlen);
    return rc;
}

/* K = HMAC_K(V || sep [|| tail]), then V = HMAC_K(V) */
static int drbg_rekey(drbg_t *g, sign_hash_t h, uint32_t hlen, uint8_t sep,
                      uint32_t tail_len)
{
    int rc;
    copy_bytes(g->m, g->V, hlen);
    g->m[hlen] = sep;
    /* the tail (x and h1) already sits in m after the separator */
    rc = hmac_h(h, g->K, g->m, hlen + 1u + tail_len, g->mac);
    if (rc != SIGN_OK)
        return rc;
    copy_bytes(g->K, g->mac, hlen);
    return drbg_next_v(g, h, hlen);
}

/*
 * Sign the reduced hash e with the private key d. RFC 6979 3.2 gives k;
 * SEC 1 4.1.3 gives r = x(kG) mod n and s = k^-1 (e + r d) mod n. A k
 * that is 0 or not below n, or that gives r or s of 0, is replaced by the
 * next candidate (3.2 step h.3).
 */
static int sign_core(const ec_t *ec, const uint32_t *d, sign_hash_t nh,
                     const uint32_t *e, uint8_t *sig)
{
    const uint32_t w = ec->words, qb = 4u * w;
    const uint32_t hlen = (nh == SIGN_SHA256) ? 32u : 64u;
    drbg_t g;
    uint32_t k[EC_MAX_WORDS], kinv[EC_MAX_WORDS], t[EC_MAX_WORDS];
    uint32_t r[EC_MAX_WORDS], s[EC_MAX_WORDS], R[2u * EC_MAX_WORDS];
    uint32_t i, got, n;
    int rc = SIGN_OK;
    bool found = false;

    /* step b, c: V = 0x01 0x01 ..., K = 0x00 0x00 ... */
    for (i = 0; i < hlen; i++) {
        g.V[i] = 0x01;
        g.K[i] = 0x00;
    }
    /* int2octets(x) and bits2octets(h1) after V and the separator */
    sign_words_to_be(g.m + hlen + 1u, d, w);
    sign_words_to_be(g.m + hlen + 1u + qb, e, w);

    /* steps d to g */
    TRY(drbg_rekey(&g, nh, hlen, 0x00, 2u * qb));
    TRY(drbg_rekey(&g, nh, hlen, 0x01, 2u * qb));

    for (n = 0; n < RFC6979_TRIES && !found; n++) {
        if (n > 0)  /* step h.3: K = HMAC_K(V || 0x00), V = HMAC_K(V) */
            TRY(drbg_rekey(&g, nh, hlen, 0x00, 0));

        /* step h.1, h.2: T from V = HMAC_K(V) until qlen bits */
        for (got = 0; got < qb; got += hlen) {
            TRY(drbg_next_v(&g, nh, hlen));
            copy_bytes(g.T + got, g.V, (qb - got < hlen) ? qb - got : hlen);
        }
        sign_be_to_words(k, g.T, w);
        if (sign_is_zero(k, w) || sign_cmp(k, ec->n, w) >= 0)
            continue;

        TRY(pke_ecc_point_mul_base(ec->id, k, R) == PKE_OK ? SIGN_OK : SIGN_ERR_HW);
        for (i = 0; i < w; i++)
            r[i] = R[i];
        sign_fix(r, ec->p, w);
        sign_fix(r, ec->n, w);
        if (sign_is_zero(r, w))
            continue;

        TRY(m_inv(kinv, k, ec->n, w));
        TRY(m_mul(t, r, d, ec->n, w));
        TRY(m_add(t, t, e, ec->n, w));
        TRY(m_mul(s, kinv, t, ec->n, w));
        if (sign_is_zero(s, w))
            continue;
        found = true;
    }

    if (found) {
        sign_words_to_be(sig, r, w);
        sign_words_to_be(sig + qb, s, w);
    } else {
        rc = SIGN_ERR_HW;
    }

done:
    sign_wipe(&g, sizeof(g));
    sign_wipe(k, sizeof(k));
    sign_wipe(kinv, sizeof(kinv));
    sign_wipe(t, sizeof(t));
    sign_wipe(R, sizeof(R));
    return rc;
}

/*
 * SEC 1 4.1.4: w = s^-1, u1 = e w, u2 = r w, R = u1 G + u2 Q, valid when
 * x(R) mod n = r. u2 is never 0 (r and w are not). u1 is 0 when e is, and
 * then R is u2 Q alone. When u1 G and u2 Q have the same x and opposite
 * y the sum is the point at infinity and the signature is rejected; this
 * is found here so the PKE never has to add a point to its negative.
 */
static int verify_core(const ec_t *ec, const uint8_t *pub, const uint32_t *e,
                       const uint8_t *sig)
{
    const uint32_t w = ec->words, qb = 4u * w;
    uint32_t r[EC_MAX_WORDS], s[EC_MAX_WORDS], winv[EC_MAX_WORDS];
    uint32_t u1[EC_MAX_WORDS], u2[EC_MAX_WORDS];
    uint32_t Q[2u * EC_MAX_WORDS], P1[2u * EC_MAX_WORDS], P2[2u * EC_MAX_WORDS];
    uint32_t i;
    int rc;

    sign_be_to_words(r, sig, w);
    sign_be_to_words(s, sig + qb, w);
    if (sign_is_zero(r, w) || sign_cmp(r, ec->n, w) >= 0 ||
        sign_is_zero(s, w) || sign_cmp(s, ec->n, w) >= 0)
        return SIGN_BAD;

    sign_be_to_words(Q, pub, w);
    sign_be_to_words(Q + w, pub + qb, w);
    TRY(check_point(ec, Q));

    TRY(m_inv(winv, s, ec->n, w));
    TRY(m_mul(u1, e, winv, ec->n, w));
    TRY(m_mul(u2, r, winv, ec->n, w));

    TRY(pke_ecc_point_mul(ec->id, u2, Q, P2) == PKE_OK ? SIGN_OK : SIGN_ERR_HW);
    if (!sign_is_zero(u1, w)) {
        TRY(pke_ecc_point_mul_base(ec->id, u1, P1) == PKE_OK ? SIGN_OK : SIGN_ERR_HW);
        if (sign_cmp(P1, P2, w) == 0 && sign_cmp(P1 + w, P2 + w, w) != 0) {
            rc = SIGN_BAD;
            goto done;
        }
        /* pke_ecc_point_add doubles when the two points are equal */
        TRY(pke_ecc_point_add(ec->id, P1, P2, P2) == PKE_OK ? SIGN_OK : SIGN_ERR_HW);
    }

    for (i = 0; i < w; i++)
        u1[i] = P2[i];
    sign_fix(u1, ec->p, w);
    sign_fix(u1, ec->n, w);
    rc = sign_cmp(u1, r, w) == 0 ? SIGN_OK : SIGN_BAD;
done:
    return rc;
}

int ecdsa_public_key(pke_curve_t curve, const uint8_t *priv, uint8_t *pub)
{
    const ec_t *ec = ec_find(curve);
    uint32_t d[EC_MAX_WORDS], Q[2u * EC_MAX_WORDS];
    int rc;

    if (ec == NULL || priv == NULL || pub == NULL)
        return SIGN_ERR_ARG;
    const uint32_t w = ec->words;

    sign_be_to_words(d, priv, w);
    if (sign_is_zero(d, w) || sign_cmp(d, ec->n, w) >= 0) {
        rc = SIGN_ERR_KEY;
        goto done;
    }
    TRY(pke_ecc_point_mul_base(curve, d, Q) == PKE_OK ? SIGN_OK : SIGN_ERR_HW);
    sign_fix(Q, ec->p, w);
    sign_fix(Q + w, ec->p, w);
    sign_words_to_be(pub, Q, w);
    sign_words_to_be(pub + 4u * w, Q + w, w);
done:
    sign_wipe(d, sizeof(d));
    return wipe_engine(rc);
}

int ecdsa_check_public_key(pke_curve_t curve, const uint8_t *pub)
{
    const ec_t *ec = ec_find(curve);
    uint32_t Q[2u * EC_MAX_WORDS];

    if (ec == NULL || pub == NULL)
        return SIGN_ERR_ARG;
    sign_be_to_words(Q, pub, ec->words);
    sign_be_to_words(Q + ec->words, pub + 4u * ec->words, ec->words);
    return check_point(ec, Q);
}

int ecdsa_sign_hash(pke_curve_t curve, const uint8_t *priv, sign_hash_t nonce_hash,
                    const uint8_t *hash, uint32_t hash_len, uint8_t *sig)
{
    const ec_t *ec = ec_find(curve);
    uint32_t d[EC_MAX_WORDS], e[EC_MAX_WORDS];
    int rc;

    if (ec == NULL || priv == NULL || hash == NULL || sig == NULL)
        return SIGN_ERR_ARG;
    if (hash_len == 0 || hash_len > HASH_MAX_BYTES)
        return SIGN_ERR_ARG;
    if (nonce_hash != SIGN_SHA256 && nonce_hash != SIGN_SHA512)
        return SIGN_ERR_ARG;
    const uint32_t w = ec->words;

    sign_be_to_words(d, priv, w);
    if (sign_is_zero(d, w) || sign_cmp(d, ec->n, w) >= 0) {
        rc = SIGN_ERR_KEY;
        goto done;
    }
    hash_to_e(ec, hash, hash_len, e);
    rc = sign_core(ec, d, nonce_hash, e, sig);
done:
    sign_wipe(d, sizeof(d));
    sign_wipe(e, sizeof(e));
    rc = wipe_engine(rc);
    if (rc != SIGN_OK)
        sign_wipe(sig, 8u * w);
    return rc;
}

int ecdsa_verify_hash(pke_curve_t curve, const uint8_t *pub,
                      const uint8_t *hash, uint32_t hash_len, const uint8_t *sig)
{
    const ec_t *ec = ec_find(curve);
    uint32_t e[EC_MAX_WORDS];

    if (ec == NULL || pub == NULL || hash == NULL || sig == NULL)
        return SIGN_ERR_ARG;
    if (hash_len == 0 || hash_len > HASH_MAX_BYTES)
        return SIGN_ERR_ARG;
    hash_to_e(ec, hash, hash_len, e);
    return verify_core(ec, pub, e, sig);
}

/* SHA-256 or SHA-512 of a whole message; *len gets 32 or 64 */
static int digest(sign_hash_t h, const uint8_t *msg, uint32_t msg_len,
                  uint8_t *out, uint32_t *len)
{
    int rc;
    if (h == SIGN_SHA256) {
        sha256_ctx_t c;
        rc = sha256_stream_init(&c);
        if (rc == SHA_OK)
            rc = sha256_stream_update(&c, msg, msg_len);
        if (rc == SHA_OK)
            rc = sha256_stream_final(&c, out);
        *len = 32;
    } else if (h == SIGN_SHA512) {
        rc = sha512_hash(msg, msg_len, out);
        *len = 64;
    } else {
        return SIGN_ERR_ARG;
    }
    return rc == SHA_OK ? SIGN_OK : SIGN_ERR_HW;
}

int ecdsa_sign(pke_curve_t curve, const uint8_t *priv, sign_hash_t hash,
               const uint8_t *msg, uint32_t msg_len, uint8_t *sig)
{
    uint8_t h[HASH_MAX_BYTES];
    uint32_t len;
    int rc;

    if (msg == NULL && msg_len != 0)
        return SIGN_ERR_ARG;
    if (ec_find(curve) == NULL || priv == NULL || sig == NULL)
        return SIGN_ERR_ARG;
    rc = digest(hash, msg, msg_len, h, &len);
    if (rc == SIGN_OK)
        rc = ecdsa_sign_hash(curve, priv, hash, h, len, sig);
    sign_wipe(h, sizeof(h));
    return rc;
}

int ecdsa_verify(pke_curve_t curve, const uint8_t *pub, sign_hash_t hash,
                 const uint8_t *msg, uint32_t msg_len, const uint8_t *sig)
{
    uint8_t h[HASH_MAX_BYTES];
    uint32_t len;
    int rc;

    if (msg == NULL && msg_len != 0)
        return SIGN_ERR_ARG;
    if (ec_find(curve) == NULL || pub == NULL || sig == NULL)
        return SIGN_ERR_ARG;
    rc = digest(hash, msg, msg_len, h, &len);
    if (rc == SIGN_OK)
        rc = ecdsa_verify_hash(curve, pub, h, len, sig);
    return rc;
}
