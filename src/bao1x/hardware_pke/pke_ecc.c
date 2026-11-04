/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * PKE elliptic curve operations for the Baochip-1x: SECG prime curves,
 * X25519 and Ed25519 point arithmetic.
 *
 * Ported from the CrossBar Daric SDK drivers/Src/sce/pke_curve.c and
 * sdma.c, with the call sequencing of middleware/crossbar/crypto/ll_api.c
 * (Copyright 2024 to 2026 CrossBar, Inc., Apache 2.0). The curve constant
 * tables are the vendor's, unchanged.
 */

#include "bao/platform.h"
#include "bao/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sce.h"
#include "hardware/pke.h"
#include "sevs_runtime.h"

#define PKE_MS_OP   2000u
#define PKE_MS_ICH   100u


/* Helpers */

static uint32_t load_le32(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static void store_le32(uint8_t *b, uint32_t w)
{
    b[0] = (uint8_t)w;
    b[1] = (uint8_t)(w >> 8);
    b[2] = (uint8_t)(w >> 16);
    b[3] = (uint8_t)(w >> 24);
}

/* Byte buffers carry no alignment guarantee, so words are built from bytes. */
static void seg_write_bytes(volatile uint32_t *seg, const uint8_t *src, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        seg[i] = load_le32(src + 4u * i);
}

static void seg_read_bytes(uint8_t *dst, const volatile uint32_t *seg, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        store_le32(dst + 4u * i, seg[i]);
}

static uint32_t bits_of_bytes(const uint8_t *b, uint32_t len)
{
    for (uint32_t j = len; j > 0; j--) {
        uint8_t v = b[j - 1];
        if (v != 0)
            return (j - 1) * 8u + (32u - (uint32_t)__builtin_clz(v));
    }
    return 0;
}

static bool aligned4(const void *p)
{
    return ((uintptr_t)p & 3u) == 0;
}

/* SCE internal DMA, segment to segment inside SCE RAM. Offsets and size
 * are in words. */
static int ich_copy(uint32_t seg_r, uint32_t off_r, uint32_t seg_w,
                    uint32_t off_w, uint32_t words)
{
    SDMA_ICH_OPT    = 0;
    SDMA_ICH_SEGID  = (SDMA_ICH_SEGID & 0xFFFF00FFu) | ((seg_r & 0xFFu) << 8);
    SDMA_ICH_ADDR_R = off_r;
    SDMA_ICH_SEGID  = (SDMA_ICH_SEGID & 0xFFFFFF00u) | (seg_w & 0xFFu);
    SDMA_ICH_ADDR_W = off_w;
    SDMA_ICH_SIZE   = words;
    memory_fence();
    SDMA_START = SDMA_ICH_START;
    memory_fence();

    uint64_t start = millis();
    while (!(SCE_DONEFR & SCE_DONE_ICH)) {
        if (millis() - start > PKE_MS_ICH)
            return PKE_ERR_TIMEOUT;
    }
    SCE_DONEFR |= SCE_DONE_ICH;
    memory_fence();
    return PKE_OK;
}

/* POB to PIB, the copy every multi step sequence uses */
static int pob_to_pib(uint32_t off_w, uint32_t words)
{
    return ich_copy(SCE_SEGID_POB, 0, SCE_SEGID_PIB, off_w, words);
}

/*
 * Load a curve: p, then the second constant (a for SECG curves, d for
 * Ed25519, (A plus 2) / 4 for X25519), then H = 2^bits(p), and run
 * ECINIT. mod_len is the bit length of the scalar about to be used, or 0.
 */
static int curve_load(const uint32_t *p, uint32_t p_words, const uint32_t *d,
                      uint32_t d_words, uint32_t mod_len)
{
    uint32_t bits = pke_bn_bits(p, p_words);

    PKE_MIMMCR = 0;
    PKE_OPTMASK = 0;
    PKE_OPTNW = bits & 0x3FFFu;
    PKE_OPTEW = mod_len & 0x3FFFu;

    uint32_t zero_bytes = p_words * 4u + d_words * 4u + bits / 8u + 4u;
    pke_seg_zero(PKE_SEG_PCON, (zero_bytes + 3u) / 4u);
    PKE_OPTLTX = 0;
    pke_seg_write(PKE_SEG_PCON, p, p_words);
    pke_seg_write(PKE_SEG_PCON + p_words, d, d_words);

    volatile uint32_t *h = PKE_SEG_PCON + p_words + d_words;
    pke_seg_zero(h, (bits / 8u + 2u + 3u) / 4u);
    h[bits / 32u] = 1u << (bits % 32u);

    PKE_PCON = 0;
    return pke_run(PKE_FN_ECINIT, PKE_MS_OP);
}

/* SECG prime curves (vendor tables, little endian words) */

/* Curve ID: secp192k1 */
static const uint32_t SECP192K1_P_A[]
    = { 0xFFFFEE37, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 };
static const uint32_t SECP192K1_G[]
    = { 0xEAE06C7D, 0x1DA5D1B1, 0x80B7F434, 0x26B07D02, 0xC057E9AE, 0xDB4FF10E, 0xD95E2F9D, 0x4082AA88, 0x15BE8634, 0x844163D0, 0x9C5628A7, 0x9B2F2F6D };

/* Curve ID: secp192r1 */
static const uint32_t SECP192R1_P_A[]
    = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFC, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
static const uint32_t SECP192R1_G[]
    = { 0x82FF1012, 0xF4FF0AFD, 0x43A18800, 0x7CBF20EB, 0xB03090F6, 0x188DA80E, 0x1E794811, 0x73F977A1, 0x6B24CDD5, 0x631011ED, 0xFFC8DA78, 0x07192B95 };

/* Curve ID: secp224k1 */
static const uint32_t SECP224K1_P_A[] = { 0xFFFFE56D, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
                                          0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 };
static const uint32_t SECP224K1_G[]
    = { 0xB6B7A45C, 0x0F7E650E, 0xE47075A9, 0x69A467E9, 0x30FC28A1, 0x4DF099DF, 0xA1455B33, 0x556D61A5, 0xE2CA4BDB, 0xC0B0BD59, 0xF7E319F7, 0x82CAFBD6, 0x7FBA3442, 0x7E089FED };

/* Curve ID: secp224r1 */
static const uint32_t SECP224R1_P_A[] = { 0x00000001, 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000,
                                          0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000 };
static const uint32_t SECP224R1_G[]
    = { 0x115C1D21, 0x343280D6, 0x56C21122, 0x4A03C1D3, 0x321390B9, 0x6BB4BF7F, 0xB70E0CBD, 0x85007E34, 0x44D58199, 0x5A074764, 0xCD4375A0, 0x4C22DFE6, 0xB5F723FB, 0xBD376388 };

/* Curve ID: secp256k1 */
static const uint32_t SECP256K1_P_A[] = { 0xFFFFFC2F, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
                                          0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000 };
static const uint32_t SECP256K1_G[]   = { 0x16F81798, 0x59F2815B, 0x2DCE28D9, 0x029BFCDB, 0xCE870B07, 0x55A06295, 0xF9DCBBAC, 0x79BE667E,
                                          0xFB10D4B8, 0x9C47D08F, 0xA6855419, 0xFD17B448, 0x0E1108A8, 0x5DA4FBFC, 0x26A3C465, 0x483ADA77 };

/* Curve ID: secp256r1 */
static const uint32_t SECP256R1_P_A[] = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF,
                                          0xFFFFFFFC, 0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0x00000000, 0x00000001, 0xFFFFFFFF };
static const uint32_t SECP256R1_G[]   = { 0xD898C296, 0xF4A13945, 0x2DEB33A0, 0x77037D81, 0x63A440F2, 0xF8BCE6E5, 0xE12C4247, 0x6B17D1F2,
                                          0x37BF51F5, 0xCBB64068, 0x6B315ECE, 0x2BCE3357, 0x7C0F9E16, 0x8EE7EB4A, 0xFE1A7F9B, 0x4FE342E2 };

/* Curve ID: secp384r1 */
static const uint32_t SECP384R1_P_A[]
    = { 0xFFFFFFFF, 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
        0xFFFFFFFC, 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF };
static const uint32_t SECP384R1_G[]
    = { 0x72760AB7, 0x3A545E38, 0xBF55296C, 0x5502F25D, 0x82542A38, 0x59F741E0, 0x8BA79B98, 0x6E1D3B62, 0xF320AD74, 0x8EB1C71E, 0xBE8B0537, 0xAA87CA22,
        0x90EA0E5F, 0x7A431D7C, 0x1D7E819D, 0x0A60B1CE, 0xB5F0B8C0, 0xE9DA3113, 0x289A147C, 0xF8F41DBD, 0x9292DC29, 0x5D9E98BF, 0x96262C6F, 0x3617DE4A };

/* Curve ID: secp521r1 */
static const uint32_t SECP521R1_P_A[]
    = { 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
        0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x000001FF, 0x00000000, 0xFFFFFFFC, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF,
        0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0x000001FF, 0x00000000 };
static const uint32_t SECP521R1_G[]
    = { 0xc2e5bd66, 0xf97e7e31, 0x856a429b, 0x3348b3c1, 0xa2ffa8de, 0xfe1dc127, 0xefe75928, 0xa14b5e77, 0x6b4d3dba, 0xf828af60, 0x053fb521, 0x9c648139,
        0x2395b442, 0x9e3ecb66, 0x0404e9cd, 0x858e06b7, 0x000000c6, 0x9fd16650, 0x88be9476, 0xa272c240, 0x353c7086, 0x3fad0761, 0xc550b901, 0x5ef42640,
        0x97ee7299, 0x273e662c, 0x17afbd17, 0x579b4468, 0x98f54449, 0x2c7d1bd9, 0x5c8a5fb4, 0x9a3bc004, 0x39296a78, 0x00000118 };

typedef struct {
    pke_curve_t     id;
    uint16_t        bits;      /* the vendor's curve length, 544 for P-521 */
    uint8_t         pad;       /* bytes of padding to a 64 bit boundary   */
    uint8_t         words;     /* words per coordinate and per scalar      */
    uint8_t         pa_words;  /* words of p (and of a) in the P_A table   */
    const uint32_t *pa;
    const uint32_t *g;
} curve_t;

static const curve_t s_curves[] = {
    { PKE_SECP192K1, 192, 0,  6,  6, SECP192K1_P_A, SECP192K1_G },
    { PKE_SECP192R1, 192, 0,  6,  6, SECP192R1_P_A, SECP192R1_G },
    { PKE_SECP224K1, 224, 4,  7,  8, SECP224K1_P_A, SECP224K1_G },
    { PKE_SECP224R1, 224, 4,  7,  8, SECP224R1_P_A, SECP224R1_G },
    { PKE_SECP256K1, 256, 0,  8,  8, SECP256K1_P_A, SECP256K1_G },
    { PKE_SECP256R1, 256, 0,  8,  8, SECP256R1_P_A, SECP256R1_G },
    { PKE_SECP384R1, 384, 0, 12, 12, SECP384R1_P_A, SECP384R1_G },
    { PKE_SECP521R1, 544, 4, 17, 18, SECP521R1_P_A, SECP521R1_G },
};

static const curve_t *find_curve(pke_curve_t id)
{
    for (uint32_t i = 0; i < sizeof(s_curves) / sizeof(s_curves[0]); i++) {
        if (s_curves[i].id == id)
            return &s_curves[i];
    }
    return NULL;
}

uint32_t pke_ecc_words(pke_curve_t curve)
{
    const curve_t *c = find_curve(curve);
    return c ? c->words : 0;
}

const uint32_t *pke_ecc_base_point(pke_curve_t curve)
{
    const curve_t *c = find_curve(curve);
    return c ? c->g : NULL;
}

static int ecc_begin(const curve_t *c)
{
    int rc = pke_sce_prepare(0);
    if (rc != PKE_OK)
        return rc;
    return curve_load(c->pa, c->pa_words, c->pa + c->pa_words, c->pa_words, 0);
}

/* Load a point into PIB: x at word 0, y one padded coordinate later */
static void load_point(const curve_t *c, const uint32_t *pt)
{
    uint32_t pad_words = (c->bits / 8u + c->pad) / 4u;
    pke_seg_write(PKE_SEG_PIB, pt, c->words);
    pke_seg_write(PKE_SEG_PIB + pad_words, pt + c->words, c->words);
}

static void read_point(const curve_t *c, uint32_t *out)
{
    uint32_t pad_words = (c->bits / 8u + c->pad) / 4u;
    pke_seg_read(out, PKE_SEG_POB, c->words);
    pke_seg_read(out + c->words, PKE_SEG_POB + pad_words, c->words);
}

#define TRY(x) do { int _rc = (x); if (_rc != PKE_OK) return _rc; } while (0)

int pke_ecc_point_mul(pke_curve_t curve, const uint32_t *k,
                      const uint32_t *p, uint32_t *out)
{
    const curve_t *c = find_curve(curve);
    if (c == NULL || k == NULL || p == NULL || out == NULL)
        return PKE_ERR_ARG;
    if (!aligned4(k) || !aligned4(p) || !aligned4(out))
        return PKE_ERR_ARG;

    uint32_t kbits = pke_bn_bits(k, c->words);
    if (kbits == 0)
        return PKE_ERR_ARG;
    if (kbits == 1) {
        /* The ECPM ladder runs OPTEW iterations, so k = 1 gives garbage.
         * 1 * p is p. */
        for (uint32_t i = 0; i < 2u * c->words; i++)
            out[i] = p[i];
        return PKE_OK;
    }

    uint32_t pad3 = (c->bits / 8u + c->pad) * 3u / 4u;

    TRY(ecc_begin(c));
    PKE_OPTEW = kbits & 0x3FFFu;
    pke_seg_write(PKE_SEG_PKB, k, c->words);
    load_point(c, p);
    PKE_OPTLTX = 0;

    PKE_PKB  = 0;
    PKE_PIB0 = 0;
    PKE_POB  = 0;
    TRY(pke_run(PKE_FN_ECI2MD, PKE_MS_OP));
    TRY(pob_to_pib(0, pad3));
    TRY(pke_run(PKE_FN_ECPD, PKE_MS_OP));
    TRY(pob_to_pib(pad3, pad3));
    PKE_PIB1 = pad3;
    TRY(pke_run(PKE_FN_ECPM, PKE_MS_OP));
    TRY(pob_to_pib(0, pad3));
    TRY(pke_run(PKE_FN_ECM2I, PKE_MS_OP));

    read_point(c, out);
    return PKE_OK;
}

int pke_ecc_point_mul_base(pke_curve_t curve, const uint32_t *k, uint32_t *out)
{
    const curve_t *c = find_curve(curve);
    if (c == NULL)
        return PKE_ERR_ARG;
    return pke_ecc_point_mul(curve, k, c->g, out);
}

int pke_ecc_point_double(pke_curve_t curve, const uint32_t *p, uint32_t *out)
{
    const curve_t *c = find_curve(curve);
    if (c == NULL || p == NULL || out == NULL || !aligned4(p) || !aligned4(out))
        return PKE_ERR_ARG;

    uint32_t pad3 = (c->bits / 8u + c->pad) * 3u / 4u;

    TRY(ecc_begin(c));
    load_point(c, p);
    PKE_OPTLTX = 0;
    PKE_PIB0 = 0;
    PKE_POB  = 0;
    TRY(pke_run(PKE_FN_ECI2MD, PKE_MS_OP));
    TRY(pob_to_pib(0, pad3));
    TRY(pke_run(PKE_FN_ECPD, PKE_MS_OP));
    TRY(pob_to_pib(0, pad3));
    TRY(pke_run(PKE_FN_ECM2I, PKE_MS_OP));

    read_point(c, out);
    return PKE_OK;
}

int pke_ecc_point_add(pke_curve_t curve, const uint32_t *a,
                      const uint32_t *b, uint32_t *out)
{
    const curve_t *c = find_curve(curve);
    if (c == NULL || a == NULL || b == NULL || out == NULL)
        return PKE_ERR_ARG;
    if (!aligned4(a) || !aligned4(b) || !aligned4(out))
        return PKE_ERR_ARG;

    bool same = true;
    for (uint32_t i = 0; i < 2u * c->words; i++) {
        if (a[i] != b[i]) {
            same = false;
            break;
        }
    }
    if (same)   /* the chord formula is undefined for a == b */
        return pke_ecc_point_double(curve, a, out);

    uint32_t pad3 = (c->bits / 8u + c->pad) * 3u / 4u;

    TRY(ecc_begin(c));
    load_point(c, a);
    PKE_OPTLTX = 0;
    PKE_PIB0 = 0;
    PKE_POB  = 0;
    TRY(pke_run(PKE_FN_ECI2MD, PKE_MS_OP));
    TRY(pob_to_pib(pad3, pad3));

    load_point(c, b);
    PKE_PIB0 = 0;
    TRY(pke_run(PKE_FN_ECI2MD, PKE_MS_OP));
    TRY(pob_to_pib(0, pad3));

    PKE_PIB0 = 0;
    PKE_PIB1 = pad3;
    TRY(pke_run(PKE_FN_ECPA, PKE_MS_OP));
    TRY(pob_to_pib(0, pad3));
    PKE_PIB0 = 0;
    TRY(pke_run(PKE_FN_ECM2I, PKE_MS_OP));

    read_point(c, out);
    return PKE_OK;
}

/* X25519 */

/* p, then (A minus 2) / 4 = 121665 (0x1DB41) */
static const uint32_t X25519_P_D[] = {
    0xffffffed, 0xffffffff, 0xffffffff, 0xffffffff,
    0xffffffff, 0xffffffff, 0xffffffff, 0x7fffffff,
    0x0001DB41, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0
};

int pke_x25519(const uint8_t k[32], const uint8_t u[32], uint8_t out[32])
{
    if (k == NULL || u == NULL || out == NULL)
        return PKE_ERR_ARG;

    uint32_t kbits = bits_of_bytes(k, 32);
    if (kbits == 0)
        return PKE_ERR_ARG;

    /* RFC 7748: the top bit of u is ignored */
    uint8_t um[32];
    for (uint32_t i = 0; i < 32; i++)
        um[i] = u[i];
    um[31] &= 0x7F;

    TRY(pke_sce_prepare(0));
    TRY(curve_load(X25519_P_D, 8, X25519_P_D + 8, 8, kbits));

    PKE_OPTLTX = 0;
    seg_write_bytes(PKE_SEG_PKB, k, 8);
    seg_write_bytes(PKE_SEG_PIB, um, 8);
    PKE_PKB  = 0;
    PKE_PIB0 = 0;
    PKE_POB  = 0;
    TRY(pke_run(PKE_FN_EDI2MD, PKE_MS_OP));
    TRY(pob_to_pib(0, 32));
    PKE_PIB0 = 0;
    PKE_POB  = 0;
    TRY(pke_run(PKE_FN_X25519, PKE_MS_OP));
    TRY(pob_to_pib(0, 32));
    PKE_PIB0 = 0;
    TRY(pke_run(PKE_FN_EDM2I, PKE_MS_OP));

    seg_read_bytes(out, PKE_SEG_POB, 8);
    return PKE_OK;
}

static void clamp(uint8_t k[32], const uint8_t priv[32])
{
    for (uint32_t i = 0; i < 32; i++)
        k[i] = priv[i];
    k[0]  &= 248;
    k[31] &= 127;
    k[31] |= 64;
}

int pke_x25519_public(const uint8_t priv[32], uint8_t pub[32])
{
    if (priv == NULL || pub == NULL)
        return PKE_ERR_ARG;
    uint8_t k[32];
    uint8_t base[32] = { 9 };
    clamp(k, priv);
    return pke_x25519(k, base, pub);
}

int pke_x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32],
                      uint8_t shared[32])
{
    if (priv == NULL || peer_pub == NULL || shared == NULL)
        return PKE_ERR_ARG;
    uint8_t k[32];
    clamp(k, priv);
    return pke_x25519(k, peer_pub, shared);
}

/* Ed25519 points */

/* p, then d */
static const uint32_t ED25519_P_D[] = {
    0xffffffed, 0xffffffff, 0xffffffff, 0xffffffff,
    0xffffffff, 0xffffffff, 0xffffffff, 0x7fffffff,
    0x135978a3, 0x75eb4dca, 0x4141d8ab, 0x00700a4d,
    0x7779e898, 0x8cc74079, 0x2b6ffe73, 0x52036cee
};

/* x then y */
static const uint32_t ED25519_BASE_POINT[] = {
    0x8f25d51a, 0xc9562d60, 0x9525a7b2, 0x692cc760,
    0xfdd6dc5c, 0xc0a4e231, 0xcd6e53fe, 0x216936d3,
    0x66666658, 0x66666666, 0x66666666, 0x66666666,
    0x66666666, 0x66666666, 0x66666666, 0x66666666
};

static int ed_load(uint32_t mod_len)
{
    return curve_load(ED25519_P_D, 8, ED25519_P_D + 8, 8, mod_len);
}

static void ed_write(volatile uint32_t *seg, const pke_ed_point_t *p)
{
    seg_write_bytes(seg, p->x, 8);
    seg_write_bytes(seg + 8, p->y, 8);
}

static void ed_read(pke_ed_point_t *p)
{
    seg_read_bytes(p->x, PKE_SEG_POB, 8);
    seg_read_bytes(p->y, PKE_SEG_POB + 8, 8);
}

int pke_ed25519_point_mul(const uint8_t k[32], const pke_ed_point_t *p,
                          pke_ed_point_t *out)
{
    if (k == NULL || p == NULL || out == NULL)
        return PKE_ERR_ARG;

    uint32_t kbits = bits_of_bytes(k, 32);
    if (kbits == 0)
        return PKE_ERR_ARG;
    if (kbits == 1) {
        *out = *p;
        return PKE_OK;
    }

    TRY(pke_sce_prepare(0));
    TRY(ed_load(kbits));

    PKE_OPTLTX = 0;
    seg_write_bytes(PKE_SEG_PKB, k, 8);
    ed_write(PKE_SEG_PIB, p);
    PKE_PKB  = 0;
    PKE_PIB0 = 0;
    PKE_POB  = 0;
    TRY(pke_run(PKE_FN_EDI2MD, PKE_MS_OP));
    TRY(pob_to_pib(0, 32));
    TRY(pke_run(PKE_FN_EDPD, PKE_MS_OP));
    TRY(pob_to_pib(32, 32));
    PKE_PIB1 = 32;
    TRY(pke_run(PKE_FN_EDPM, PKE_MS_OP));
    TRY(pob_to_pib(0, 32));
    TRY(pke_run(PKE_FN_EDM2I, PKE_MS_OP));

    ed_read(out);
    return PKE_OK;
}

int pke_ed25519_point_mul_base(const uint8_t k[32], pke_ed_point_t *out)
{
    pke_ed_point_t g;
    for (uint32_t i = 0; i < 8; i++) {
        store_le32(g.x + 4u * i, ED25519_BASE_POINT[i]);
        store_le32(g.y + 4u * i, ED25519_BASE_POINT[8 + i]);
    }
    return pke_ed25519_point_mul(k, &g, out);
}

int pke_ed25519_point_double(const pke_ed_point_t *p, pke_ed_point_t *out)
{
    if (p == NULL || out == NULL)
        return PKE_ERR_ARG;

    TRY(pke_sce_prepare(0));
    TRY(ed_load(0));

    PKE_OPTLTX = 0;
    ed_write(PKE_SEG_PIB, p);
    PKE_PIB0 = 0;
    PKE_POB  = 0;
    TRY(pke_run(PKE_FN_EDI2MD, PKE_MS_OP));
    TRY(pob_to_pib(0, 32));
    TRY(pke_run(PKE_FN_EDPD, PKE_MS_OP));
    TRY(pob_to_pib(0, 32));
    TRY(pke_run(PKE_FN_EDM2I, PKE_MS_OP));

    ed_read(out);
    return PKE_OK;
}

int pke_ed25519_point_add(const pke_ed_point_t *a, const pke_ed_point_t *b,
                          pke_ed_point_t *out)
{
    if (a == NULL || b == NULL || out == NULL)
        return PKE_ERR_ARG;

    TRY(pke_sce_prepare(0));
    TRY(ed_load(0));

    PKE_OPTLTX = 0;
    ed_write(PKE_SEG_PIB, a);
    PKE_PIB0 = 0;
    PKE_POB  = 0;
    TRY(pke_run(PKE_FN_EDI2MD, PKE_MS_OP));
    TRY(pob_to_pib(32, 32));

    /* The vendor reloads the curve between the two conversions */
    TRY(ed_load(0));

    PKE_OPTLTX = 0;
    ed_write(PKE_SEG_PIB, b);
    PKE_PIB0 = 0;
    TRY(pke_run(PKE_FN_EDI2MD, PKE_MS_OP));
    TRY(pob_to_pib(0, 32));

    PKE_PIB0 = 0;
    PKE_PIB1 = 32;
    TRY(pke_run(PKE_FN_EDPA, PKE_MS_OP));
    TRY(pob_to_pib(0, 32));
    PKE_PIB0 = 0;
    TRY(pke_run(PKE_FN_EDM2I, PKE_MS_OP));

    ed_read(out);
    return PKE_OK;
}
