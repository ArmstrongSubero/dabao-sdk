/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * ALU driver for the Baochip-1x SCE.
 *
 * Ported from the CrossBar Daric SDK drivers/Src/sce/alu.c and the
 * bndivision_le() wrapper in middleware/crossbar/crypto/ll_api.c
 * (Copyright 2024-2026 CrossBar, Inc., Apache 2.0).
 */

#include "bao/platform.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sce.h"
#include "hardware/pke.h"
#include "hardware/alu.h"
#include "sevs_runtime.h"

#define ALU_MS_OP      2000u
#define ALU_MAX_WORDS   256u

static int alu_status(int rc)
{
    return rc == PKE_OK ? ALU_OK : ALU_ERR_TIMEOUT;
}

static bool aligned4(const void *p)
{
    return ((uintptr_t)p & 3u) == 0;
}

static uint32_t even_words(uint32_t w)
{
    return (w + 1u) & ~1u;
}

static void alu_clock_on(void)
{
    SCE_SCEMODE   = 0;
    SCE_SUBCLKEN |= SCE_CLK_ALU;
    memory_fence();
}

static void set_lengths(uint32_t de_dwords, uint32_t ds_dwords)
{
    ALU_CRDIVLEN = (ALU_CRDIVLEN & 0xFFFF0000u) | (de_dwords & 0xFFu) |
                   ((ds_dwords & 0xFFu) << 8);
}

static void set_pointers(uint32_t qt_seg, uint32_t rm_seg)
{
    ALU_SEGPTR_DE = ALU_SEGPTR(SCE_SEGID_PIB, 0);
    ALU_SEGPTR_DS = ALU_SEGPTR(SCE_SEGID_PIB, 256);
    ALU_SEGPTR_QT = ALU_SEGPTR(qt_seg, 0);
    ALU_SEGPTR_RM = ALU_SEGPTR(rm_seg, 0);
}

static int alu_run(uint32_t fn)
{
    ALU_CRFUNC = fn & 0xFFu;
    ALU_FR = 0xFF;
    memory_fence();
    ALU_AR = 0x5A;
    memory_fence();
    return alu_status(pke_wait_fr(&ALU_FR, ALU_MS_OP));
}

/* The ALU reads little endian operands with the two words of each 64 bit
 * unit swapped (an RTL quirk the vendor works around in software). */
static void swap_pairs(volatile uint32_t *seg, uint32_t words)
{
    for (uint32_t i = 0; i + 1 < words; i += 2) {
        uint32_t t = seg[i];
        seg[i]     = seg[i + 1];
        seg[i + 1] = t;
    }
}

static int cmp_words(const uint32_t *a, uint32_t aw, const uint32_t *b, uint32_t bw)
{
    if (aw != bw)
        return aw > bw ? 1 : -1;
    for (uint32_t i = aw; i > 0; i--) {
        if (a[i - 1] != b[i - 1])
            return a[i - 1] > b[i - 1] ? 1 : -1;
    }
    return 0;
}

static void put_small(uint32_t *dst, uint32_t *len, uint64_t v)
{
    if (dst != NULL) {
        dst[0] = (uint32_t)v;
        dst[1] = (uint32_t)(v >> 32);
    }
    if (len != NULL)
        *len = (v >> 32) ? 2u : 1u;
}

int alu_div(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words,
            uint32_t *q, uint32_t *q_words,
            uint32_t *r, uint32_t *r_words)
{
    if (x == NULL || y == NULL || !aligned4(x) || !aligned4(y))
        return ALU_ERR_ARG;
    if (q == NULL && r == NULL)
        return ALU_ERR_ARG;
    if ((q != NULL && !aligned4(q)) || (r != NULL && !aligned4(r)))
        return ALU_ERR_ARG;
    if (x_words > ALU_MAX_WORDS || y_words > ALU_MAX_WORDS)
        return ALU_ERR_ARG;

    uint32_t xw = pke_bn_words(x, x_words);
    uint32_t yw = pke_bn_words(y, y_words);
    if (yw == 0)
        return ALU_ERR_ARG;
    if (pke_bn_bits(x, xw) > 8190 || pke_bn_bits(y, yw) > 8190)
        return ALU_ERR_ARG;

    int c = cmp_words(x, xw, y, yw);
    if (c < 0) {
        if (q != NULL) q[0] = 0;
        if (q_words != NULL) *q_words = 1;
        if (r != NULL) {
            for (uint32_t i = 0; i < xw; i++)
                r[i] = x[i];
            if (xw == 0)
                r[0] = 0;
        }
        if (r_words != NULL) *r_words = xw ? xw : 1u;
        return ALU_OK;
    }
    if (c == 0) {
        if (q != NULL) q[0] = 1;
        if (q_words != NULL) *q_words = 1;
        if (r != NULL) r[0] = 0;
        if (r_words != NULL) *r_words = 1;
        return ALU_OK;
    }

    /* The hardware wants a dividend of at least 65 bits; below that both
     * operands fit in 64 bits and plain division does it. */
    if (pke_bn_bits(x, xw) <= 64) {
        uint64_t a = x[0] | ((xw > 1) ? ((uint64_t)x[1] << 32) : 0);
        uint64_t b = y[0] | ((yw > 1) ? ((uint64_t)y[1] << 32) : 0);
        put_small(q, q_words, a / b);
        put_small(r, r_words, a % b);
        return ALU_OK;
    }

    uint32_t xp = even_words(xw);
    uint32_t yp = even_words(yw);

    alu_clock_on();
    set_lengths(xp / 2u, yp / 2u);

    pke_seg_zero(PKE_SEG_PIB, xp);
    pke_seg_write(PKE_SEG_PIB, x, xw);
    pke_seg_zero(PKE_SEG_PSIB, yp);
    pke_seg_write(PKE_SEG_PSIB, y, yw);
    swap_pairs(PKE_SEG_PIB, xp);
    swap_pairs(PKE_SEG_PSIB, yp);

    ALU_OPTLTX = 0x00;
    set_pointers(SCE_SEGID_POB, SCE_SEGID_PSOB);

    int rc = alu_run(ALU_FN_DIV);
    if (rc != ALU_OK)
        return rc;

    uint32_t qws = (ALU_SRDIVLEN & 0xFFu) * 2u;
    uint32_t rws = ((ALU_SRDIVLEN >> 8) & 0xFFu) * 2u;
    /* A dividend longer than 126 words reports 0 here; the quotient is
     * then the full 128 words. */
    if (qws == 0 && xp > 126)
        qws = 128;
    if (qws > xp)
        qws = xp;
    if (rws > yp)
        rws = yp;

    if (q != NULL)
        pke_seg_read(q, PKE_SEG_POB, qws);
    if (q_words != NULL)
        *q_words = qws;
    if (r != NULL)
        pke_seg_read(r, PKE_SEG_PSOB, rws);
    if (r_words != NULL)
        *r_words = rws;
    return ALU_OK;
}

static int addsub(uint32_t fn, const uint32_t *x, uint32_t x_words,
                  const uint32_t *y, uint32_t y_words,
                  uint32_t *out, uint32_t *flag)
{
    if (x == NULL || y == NULL || out == NULL)
        return ALU_ERR_ARG;
    if (!aligned4(x) || !aligned4(y) || !aligned4(out))
        return ALU_ERR_ARG;
    if (x_words == 0 || y_words == 0 || (x_words & 1u) || (y_words & 1u))
        return ALU_ERR_ARG;
    if (x_words > ALU_MAX_WORDS || y_words > ALU_MAX_WORDS)
        return ALU_ERR_ARG;
    if (pke_bn_bits(x, x_words) > 8190 || pke_bn_bits(y, y_words) > 8190)
        return ALU_ERR_ARG;
    if (fn == ALU_FN_SUB && x_words < y_words)
        return ALU_ERR_ARG;

    alu_clock_on();
    set_lengths(x_words / 2u, y_words / 2u);

    pke_seg_write(PKE_SEG_PIB, x, x_words);
    pke_seg_write(PKE_SEG_PSIB, y, y_words);
    swap_pairs(PKE_SEG_PIB, x_words);
    swap_pairs(PKE_SEG_PSIB, y_words);

    ALU_OPTLTX = 0x00;
    set_pointers(SCE_SEGID_PSOB, SCE_SEGID_POB);

    int rc = alu_run(fn);
    if (rc != ALU_OK)
        return rc;

    if (flag != NULL)
        *flag = (ALU_SRMFSM & 0x100u) ? 1u : 0u;

    uint32_t n = (fn == ALU_FN_SUB) ? x_words : (x_words > y_words ? x_words : y_words);
    pke_seg_read(out, PKE_SEG_PSOB, n);
    return ALU_OK;
}

int alu_add(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words,
            uint32_t *sum, uint32_t *carry)
{
    return addsub(ALU_FN_ADD, x, x_words, y, y_words, sum, carry);
}

int alu_sub(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words,
            uint32_t *diff, uint32_t *borrow)
{
    return addsub(ALU_FN_SUB, x, x_words, y, y_words, diff, borrow);
}

static int shift(uint32_t fn, const uint32_t *in, uint32_t words,
                 uint32_t count, uint32_t *out)
{
    if (in == NULL || out == NULL || !aligned4(in) || !aligned4(out))
        return ALU_ERR_ARG;
    if (words == 0 || words > ALU_MAX_WORDS || count > 255)
        return ALU_ERR_ARG;

    uint32_t xa = even_words(words);

    alu_clock_on();
    set_lengths(xa / 2u, 0);

    pke_seg_zero(PKE_SEG_PIB, xa);
    pke_seg_write(PKE_SEG_PIB + (xa - words), in, words);

    ALU_OPT = (ALU_OPT & 0x0000FFF8u);                         /* right */
    ALU_OPT = (ALU_OPT & 0x000000FFu) | ((count & 0xFFu) << 8);
    ALU_OPTLTX = 0xFF;
    set_pointers(SCE_SEGID_PSOB, SCE_SEGID_POB);

    int rc = alu_run(fn);
    if (rc != ALU_OK)
        return rc;

    pke_seg_read(out, PKE_SEG_PSOB, xa);
    return ALU_OK;
}

int alu_shift_right(const uint32_t *in, uint32_t words, uint32_t bits,
                    uint32_t *out)
{
    return shift(ALU_FN_SFT, in, words, bits, out);
}

int alu_shift_right_words(const uint32_t *in, uint32_t words, uint32_t count,
                          uint32_t *out)
{
    return shift(ALU_FN_SFTW, in, words, count, out);
}

int alu_logic(const uint32_t *x, uint32_t x_words,
              const uint32_t *y, uint32_t y_words,
              uint32_t op, uint32_t *out)
{
    if (x == NULL || y == NULL || out == NULL)
        return ALU_ERR_ARG;
    if (!aligned4(x) || !aligned4(y) || !aligned4(out))
        return ALU_ERR_ARG;
    if (x_words == 0 || y_words == 0 || x_words > ALU_MAX_WORDS || y_words > ALU_MAX_WORDS)
        return ALU_ERR_ARG;
    if (op & ~0xF0u)
        return ALU_ERR_ARG;

    uint32_t xa = even_words(x_words);
    uint32_t ya = even_words(y_words);

    alu_clock_on();
    set_lengths(xa / 2u, ya / 2u);

    pke_seg_zero(PKE_SEG_PIB, xa);
    pke_seg_zero(PKE_SEG_PSIB, ya);
    pke_seg_write(PKE_SEG_PIB + (xa - x_words), x, x_words);
    pke_seg_write(PKE_SEG_PSIB + (ya - y_words), y, y_words);

    ALU_OPT = op;
    ALU_OPTLTX = 0xFF;
    set_pointers(SCE_SEGID_PSOB, SCE_SEGID_POB);

    int rc = alu_run(ALU_FN_BLG);
    if (rc != ALU_OK)
        return rc;

    pke_seg_read(out, PKE_SEG_PSOB, xa >= ya ? xa : ya);
    return ALU_OK;
}

int alu_byte_exchange(const uint32_t *in, uint32_t words, uint32_t *out)
{
    if (in == NULL || out == NULL || !aligned4(in) || !aligned4(out))
        return ALU_ERR_ARG;
    if (words == 0 || words > 252)
        return ALU_ERR_ARG;

    uint32_t xa = even_words(words);

    alu_clock_on();
    set_lengths(xa / 2u + 1u, xa / 2u + 1u);

    pke_seg_zero(PKE_SEG_PIB, xa);
    pke_seg_write(PKE_SEG_PIB + (xa - words), in, words);

    ALU_OPT = 0;
    ALU_OPTLTX = 0x00;
    set_pointers(SCE_SEGID_PSOB, SCE_SEGID_POB);

    int rc = alu_run(ALU_FN_BEX);
    if (rc != ALU_OK)
        return rc;

    pke_seg_read(out, PKE_SEG_POB + 2, xa);
    return ALU_OK;
}

/* Modular arithmetic on the ALU */

#define MW ALU_MOD_MAX_WORDS

static uint32_t m_a[MW], m_b[MW], m_prod[2 * MW + 2], m_rem[MW + 2];
static uint32_t m_t[MW + 2], m_r2[MW], m_xm[MW], m_acc[MW], m_one[MW];
static uint32_t i_r0[MW], i_r1[MW], i_t0[MW], i_t1[MW], i_t2[MW];
static uint32_t i_q[MW + 2], i_rr[MW + 2];

static uint32_t bn_len(const uint32_t *a, uint32_t w)
{
    while (w > 0 && a[w - 1] == 0)
        w--;
    return w;
}

static void bn_copy(uint32_t *d, const uint32_t *s, uint32_t sw, uint32_t dw)
{
    for (uint32_t i = 0; i < dw; i++)
        d[i] = (i < sw) ? s[i] : 0;
}

static int bn_cmp(const uint32_t *a, const uint32_t *b, uint32_t w)
{
    for (uint32_t i = w; i > 0; i--) {
        if (a[i - 1] != b[i - 1])
            return a[i - 1] > b[i - 1] ? 1 : -1;
    }
    return 0;
}

static uint32_t bn_add(uint32_t *r, const uint32_t *a, const uint32_t *b, uint32_t w)
{
    uint64_t c = 0;
    for (uint32_t i = 0; i < w; i++) {
        c += (uint64_t)a[i] + b[i];
        r[i] = (uint32_t)c;
        c >>= 32;
    }
    return (uint32_t)c;
}

static uint32_t bn_sub(uint32_t *r, const uint32_t *a, const uint32_t *b, uint32_t w)
{
    uint32_t borrow = 0;
    for (uint32_t i = 0; i < w; i++) {
        uint64_t d = (uint64_t)a[i] - b[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (uint32_t)(d >> 63);
    }
    return borrow;
}

static int bn_is_zero(const uint32_t *a, uint32_t w)
{
    return bn_len(a, w) == 0;
}

/* out (nw words) = a mod n, using the ALU */
static int bn_reduce(const uint32_t *a, uint32_t aw, const uint32_t *n,
                     uint32_t nw, uint32_t *out)
{
    uint32_t al = bn_len(a, aw);
    uint32_t rw = 0;

    if (al == 0) {
        bn_copy(out, a, 0, nw);
        return ALU_OK;
    }
    int rc = alu_div(a, al, n, nw, NULL, NULL, m_rem, &rw);
    if (rc != ALU_OK)
        return rc;
    if (rw > nw)
        rw = nw;
    bn_copy(out, m_rem, rw, nw);
    return ALU_OK;
}

/* out = a * b mod n, a and b already below n, all nw words */
static int bn_mulmod(const uint32_t *a, const uint32_t *b, const uint32_t *n,
                     uint32_t nw, uint32_t *out)
{
    for (uint32_t i = 0; i < 2 * nw; i++)
        m_prod[i] = 0;
    for (uint32_t i = 0; i < nw; i++) {
        uint64_t c = 0;
        for (uint32_t j = 0; j < nw; j++) {
            c += (uint64_t)a[j] * b[i] + m_prod[i + j];
            m_prod[i + j] = (uint32_t)c;
            c >>= 32;
        }
        m_prod[i + nw] = (uint32_t)c;
    }
    return bn_reduce(m_prod, 2 * nw, n, nw, out);
}

/* Check the arguments and return the significant length of n, or 0 */
static uint32_t mod_args(const uint32_t *n, uint32_t n_words, const uint32_t *out)
{
    if (n == NULL || out == NULL || !aligned4(n) || !aligned4(out))
        return 0;
    if (n_words == 0 || n_words > MW)
        return 0;
    return bn_len(n, n_words);
}

static int operand_ok(const uint32_t *x, uint32_t w)
{
    return x != NULL && aligned4(x) && w > 0 && w <= MW;
}

static void put_out(uint32_t *out, const uint32_t *v, uint32_t vw, uint32_t n_words)
{
    bn_copy(out, v, vw, n_words);
}

int alu_mod_add(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    uint32_t nl = mod_args(n, n_words, out);
    if (nl == 0 || !operand_ok(x, x_words) || !operand_ok(y, y_words))
        return ALU_ERR_ARG;

    int rc = bn_reduce(x, x_words, n, nl, m_a);
    if (rc == ALU_OK)
        rc = bn_reduce(y, y_words, n, nl, m_b);
    if (rc != ALU_OK)
        return rc;

    uint32_t carry = bn_add(m_t, m_a, m_b, nl);
    if (carry || bn_cmp(m_t, n, nl) >= 0)
        bn_sub(m_t, m_t, n, nl);
    put_out(out, m_t, nl, n_words);
    return ALU_OK;
}

int alu_mod_sub(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    uint32_t nl = mod_args(n, n_words, out);
    if (nl == 0 || !operand_ok(x, x_words) || !operand_ok(y, y_words))
        return ALU_ERR_ARG;

    int rc = bn_reduce(x, x_words, n, nl, m_a);
    if (rc == ALU_OK)
        rc = bn_reduce(y, y_words, n, nl, m_b);
    if (rc != ALU_OK)
        return rc;

    if (bn_sub(m_t, m_a, m_b, nl))
        bn_add(m_t, m_t, n, nl);
    put_out(out, m_t, nl, n_words);
    return ALU_OK;
}

int alu_mod_mul(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    uint32_t nl = mod_args(n, n_words, out);
    if (nl == 0 || !operand_ok(x, x_words) || !operand_ok(y, y_words))
        return ALU_ERR_ARG;

    int rc = bn_reduce(x, x_words, n, nl, m_a);
    if (rc == ALU_OK)
        rc = bn_reduce(y, y_words, n, nl, m_b);
    if (rc == ALU_OK)
        rc = bn_mulmod(m_a, m_b, n, nl, m_t);
    if (rc != ALU_OK)
        return rc;
    put_out(out, m_t, nl, n_words);
    return ALU_OK;
}

/* Montgomery product for an odd modulus: out = a * b / 2^(32 nw) mod n */
static void mont_mul(uint32_t *out, const uint32_t *a, const uint32_t *b,
                     const uint32_t *n, uint32_t nw, uint32_t ninv)
{
    for (uint32_t i = 0; i < nw + 2; i++)
        m_t[i] = 0;

    for (uint32_t i = 0; i < nw; i++) {
        uint64_t c = 0;
        for (uint32_t j = 0; j < nw; j++) {
            c += (uint64_t)a[j] * b[i] + m_t[j];
            m_t[j] = (uint32_t)c;
            c >>= 32;
        }
        c += m_t[nw];
        m_t[nw] = (uint32_t)c;
        m_t[nw + 1] = (uint32_t)(c >> 32);

        uint32_t m = m_t[0] * ninv;
        c = (uint64_t)m * n[0] + m_t[0];
        c >>= 32;
        for (uint32_t j = 1; j < nw; j++) {
            c += (uint64_t)m * n[j] + m_t[j];
            m_t[j - 1] = (uint32_t)c;
            c >>= 32;
        }
        c += m_t[nw];
        m_t[nw - 1] = (uint32_t)c;
        m_t[nw] = m_t[nw + 1] + (uint32_t)(c >> 32);
    }

    if (m_t[nw] || bn_cmp(m_t, n, nw) >= 0)
        bn_sub(m_t, m_t, n, nw);
    bn_copy(out, m_t, nw, nw);
}

static int exp_odd(const uint32_t *e, uint32_t el, const uint32_t *n,
                   uint32_t nl, uint32_t *out)
{
    uint32_t inv = 1;
    for (int i = 0; i < 5; i++)
        inv *= 2u - n[0] * inv;
    uint32_t ninv = 0u - inv;

    /* R^2 mod n, with R = 2^(32 nl) */
    for (uint32_t i = 0; i < 2 * nl + 1; i++)
        m_prod[i] = 0;
    m_prod[2 * nl] = 1;
    int rc = bn_reduce(m_prod, 2 * nl + 1, n, nl, m_r2);
    if (rc != ALU_OK)
        return rc;

    bn_copy(m_one, NULL, 0, nl);
    m_one[0] = 1;

    mont_mul(m_xm, m_a, m_r2, n, nl, ninv);
    mont_mul(m_acc, m_one, m_r2, n, nl, ninv);

    for (uint32_t w = el; w > 0; w--) {
        for (int bit = 31; bit >= 0; bit--) {
            mont_mul(m_acc, m_acc, m_acc, n, nl, ninv);
            if ((e[w - 1] >> bit) & 1u)
                mont_mul(m_acc, m_acc, m_xm, n, nl, ninv);
        }
    }
    mont_mul(out, m_acc, m_one, n, nl, ninv);
    return ALU_OK;
}

static int exp_even(const uint32_t *e, uint32_t el, const uint32_t *n,
                    uint32_t nl, uint32_t *out)
{
    int rc;

    bn_copy(m_acc, NULL, 0, nl);
    m_acc[0] = 1;
    rc = bn_reduce(m_acc, nl, n, nl, m_acc);
    if (rc != ALU_OK)
        return rc;

    for (uint32_t w = el; w > 0; w--) {
        for (int bit = 31; bit >= 0; bit--) {
            rc = bn_mulmod(m_acc, m_acc, n, nl, m_acc);
            if (rc == ALU_OK && ((e[w - 1] >> bit) & 1u))
                rc = bn_mulmod(m_acc, m_a, n, nl, m_acc);
            if (rc != ALU_OK)
                return rc;
        }
    }
    bn_copy(out, m_acc, nl, nl);
    return ALU_OK;
}

int alu_mod_exp(const uint32_t *x, uint32_t x_words,
                const uint32_t *e, uint32_t e_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    uint32_t nl = mod_args(n, n_words, out);
    if (nl == 0 || !operand_ok(x, x_words) || !operand_ok(e, e_words))
        return ALU_ERR_ARG;

    int rc = bn_reduce(x, x_words, n, nl, m_a);
    if (rc != ALU_OK)
        return rc;

    uint32_t el = bn_len(e, e_words);
    if (n[0] & 1u)
        rc = exp_odd(e, el, n, nl, m_b);
    else
        rc = exp_even(e, el, n, nl, m_b);
    if (rc != ALU_OK)
        return rc;
    put_out(out, m_b, nl, n_words);
    return ALU_OK;
}

int alu_mod_inv(const uint32_t *x, uint32_t x_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    uint32_t nl = mod_args(n, n_words, out);
    if (nl == 0 || !operand_ok(x, x_words))
        return ALU_ERR_ARG;

    /* Extended Euclid with t kept mod n: t_i * x = r_i (mod n) */
    bn_copy(i_r0, n, nl, nl);
    int rc = bn_reduce(x, x_words, n, nl, i_r1);
    if (rc != ALU_OK)
        return rc;
    bn_copy(i_t0, NULL, 0, nl);
    bn_copy(i_t1, NULL, 0, nl);
    i_t1[0] = 1;

    while (!bn_is_zero(i_r1, nl)) {
        uint32_t qw = 0, rw = 0;
        uint32_t l0 = bn_len(i_r0, nl);
        uint32_t l1 = bn_len(i_r1, nl);

        rc = alu_div(i_r0, l0, i_r1, l1, i_q, &qw, i_rr, &rw);
        if (rc != ALU_OK)
            return rc;
        if (qw > nl + 1)
            qw = nl + 1;

        rc = bn_reduce(i_q, qw, n, nl, m_a);
        if (rc == ALU_OK)
            rc = bn_mulmod(m_a, i_t1, n, nl, m_b);
        if (rc != ALU_OK)
            return rc;
        if (bn_sub(i_t2, i_t0, m_b, nl))
            bn_add(i_t2, i_t2, n, nl);

        bn_copy(i_r0, i_r1, nl, nl);
        bn_copy(i_r1, i_rr, rw < nl ? rw : nl, nl);
        bn_copy(i_t0, i_t1, nl, nl);
        bn_copy(i_t1, i_t2, nl, nl);
    }

    if (bn_len(i_r0, nl) != 1 || i_r0[0] != 1)
        return ALU_ERR_NOT_COPRIME;
    put_out(out, i_t0, nl, n_words);
    return ALU_OK;
}

int alu_gcd(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words, uint32_t *out)
{
    if (!operand_ok(x, x_words) || !operand_ok(y, y_words))
        return ALU_ERR_ARG;
    if (out == NULL || !aligned4(out))
        return ALU_ERR_ARG;

    uint32_t w = x_words > y_words ? x_words : y_words;
    bn_copy(i_r0, x, x_words, w);
    bn_copy(i_r1, y, y_words, w);

    while (!bn_is_zero(i_r1, w)) {
        uint32_t rw = 0;
        uint32_t l0 = bn_len(i_r0, w);
        uint32_t l1 = bn_len(i_r1, w);
        int rc;

        if (l0 == 0) {
            bn_copy(i_r0, NULL, 0, w);
            rw = 0;
        } else {
            rc = alu_div(i_r0, l0, i_r1, l1, NULL, NULL, i_rr, &rw);
            if (rc != ALU_OK)
                return rc;
        }
        bn_copy(i_r0, i_r1, w, w);
        bn_copy(i_r1, i_rr, rw < w ? rw : w, w);
    }
    bn_copy(out, i_r0, w, w);
    return ALU_OK;
}
