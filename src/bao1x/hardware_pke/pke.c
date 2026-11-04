/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * PKE driver for the Baochip-1x: SCE preparation, shared helpers and
 * modular arithmetic on an arbitrary modulus.
 *
 * Ported from the CrossBar Daric SDK drivers/Src/sce/pke_rsa.c and the
 * sequencing in middleware/crossbar/crypto/ll_api.c (Copyright 2024 to 2026
 * CrossBar, Inc., Apache 2.0).
 */

#include "bao/platform.h"
#include "bao/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sce.h"
#include "hardware/pke.h"
#include "sevs_runtime.h"

#define PKE_MS_OP        2000u
#define PKE_MS_EXP      30000u
#define PKE_MS_RAMCLR     100u
#define PKE_MS_INV        200u

#define PKE_MAX_WORDS     256u
#define PKE_INV_MAX_WORDS 128u

/* Shared helpers */

void pke_seg_write(volatile uint32_t *seg, const uint32_t *src, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        seg[i] = src[i];
}

void pke_seg_zero(volatile uint32_t *seg, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        seg[i] = 0;
}

void pke_seg_read(uint32_t *dst, const volatile uint32_t *seg, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        dst[i] = seg[i];
}

uint32_t pke_bn_words(const uint32_t *x, uint32_t words)
{
    while (words > 0 && x[words - 1] == 0)
        words--;
    return words;
}

uint32_t pke_bn_bits(const uint32_t *x, uint32_t words)
{
    uint32_t w = pke_bn_words(x, words);
    if (w == 0)
        return 0;
    return (w - 1) * 32u + (32u - (uint32_t)__builtin_clz(x[w - 1]));
}

static bool aligned4(const void *p)
{
    return ((uintptr_t)p & 3u) == 0;
}

static int wait_ramclr(void)
{
    uint64_t start = millis();
    while (SCE_BUSYSR & SCE_BUSY_RAMCLR) {
        if (millis() - start > PKE_MS_RAMCLR)
            return PKE_ERR_TIMEOUT;
    }
    return PKE_OK;
}

/*
 * The vendor library runs this before every PKE operation: reset the SCE
 * control state, put the clocks back, then zero the SCE RAM. The clocks
 * and FIFO enables the AES, HASH and TRNG drivers set are restored too, so
 * those drivers keep working; only the contents of SCE RAM (the AES key
 * among them) are lost. SCEMODE goes back to normal, which is what the
 * AES and SHA drivers already write.
 */
/* PKE clock: PLL1 to VCO / 12 (about 174 MHz), PLL0 unchanged, PKE divider
 * 0xFF. boot1 leaves PLL1 at the raw VCO, where the PKE fails. */
#define CGU_SEC      (*(volatile uint32_t *)(0x40040000u + 0x00u))
#define CGU_SEL0     (*(volatile uint32_t *)(0x40040000u + 0x10u))
#define CGU_SET      (*(volatile uint32_t *)(0x40040000u + 0x2Cu))
#define CGU_FDPKE    (*(volatile uint32_t *)(0x40040000u + 0x34u))
#define CGU_FSFREQ0  (*(volatile uint32_t *)(0x40040000u + 0x40u))
#define CGU_FSFREQ3  (*(volatile uint32_t *)(0x40040000u + 0x4Cu))
#define CGU_FSVLD    (*(volatile uint32_t *)(0x40040000u + 0x50u))
#define CGU_ARIPFLOW (*(volatile uint32_t *)(0x40040000u + 0x90u))
#define CGU_LPEN     (*(volatile uint32_t *)(0x40040000u + 0x98u))
#define CGU_PLLMN    (*(volatile uint32_t *)(0x40040000u + 0xA0u))
#define CGU_PLLQ     (*(volatile uint32_t *)(0x40040000u + 0xA8u))
#define PLL1_Q       0x2300u     /* PLL1 q0 = 4, q1 = 3 */
#define PKE_FD       0xFFu

static bool s_pke_clock_set;

static void spin(uint32_t n)
{
    for (volatile uint32_t i = 0; i < n; i++) {
    }
}

static void pll1_setup(void)
{
    uint32_t q = CGU_PLLQ;
    uint32_t nq = (q & 0xFFu) | PLL1_Q;
    if (q == nq)
        return;

    delay_ms(5);
    uint32_t mstatus;
    __asm__ volatile ("csrrci %0, mstatus, 8" : "=r"(mstatus));

    CGU_SEL0 = 0;
    CGU_SET  = 0x32u;
    memory_fence();
    spin(1000);

    CGU_LPEN |= 0x2u;
    CGU_ARIPFLOW = 0x32u;
    memory_fence();
    spin(4096);

    CGU_PLLQ = nq;
    CGU_ARIPFLOW = 0x32u;
    memory_fence();

    CGU_LPEN &= ~0x2u;
    CGU_ARIPFLOW = 0x32u;
    memory_fence();
    spin(200000);

    CGU_SEL0 = 1;
    CGU_SET  = 0x32u;
    memory_fence();
    spin(1000);

    __asm__ volatile ("csrs mstatus, %0" :: "r"(mstatus & 8u));
}

void pke_set_clock_divider(uint32_t fd)
{
    CGU_SEC = CGU_SEC & ~(1u << 12);
    memory_fence();
    CGU_FDPKE = fd & 0xFFu;
    memory_fence();
    CGU_SET = 0x32u;
    memory_fence();
    s_pke_clock_set = true;
}

void pke_clock_init(void)
{
    pll1_setup();
    pke_set_clock_divider(PKE_FD);
}

void pke_clock_status(pke_clock_status_t *s)
{
    if (s == NULL)
        return;
    s->pllmn    = CGU_PLLMN;
    s->pllq     = CGU_PLLQ;
    s->cgusec   = CGU_SEC;
    s->cgusel0  = CGU_SEL0;
    s->fdpke    = CGU_FDPKE;
    s->fsvld    = CGU_FSVLD;
    uint32_t f0 = CGU_FSFREQ0;
    uint32_t f3 = CGU_FSFREQ3;
    s->fclk_mhz = f0 >> 16;
    s->pke_mhz  = f0 & 0xFFFFu;
    s->pll0_mhz = f3 >> 16;
    s->pll1_mhz = f3 & 0xFFFFu;
}

static void pke_clock_setup(void)
{
    if (!s_pke_clock_set)
        pke_clock_init();
}

int pke_sce_prepare(uint32_t extra_clocks)
{
    (void)extra_clocks;
    /* The Daric SDK's SUBEN bit order does not match the RTL, so turn every
     * SCE block clock on. */
    uint32_t clk  = 0x1Fu;
    uint32_t fifo = SCE_FIFOEN;

    pke_clock_setup();

    SCE_SCEMODE  = 0;
    SCE_SUBCLKEN = clk;
    memory_fence();

    SCE_RESETRAM = SCE_RESETRAM_CTRL;
    memory_fence();
    if (wait_ramclr() != PKE_OK)
        return PKE_ERR_TIMEOUT;

    SCE_SCEMODE  = 0;
    SCE_SUBCLKEN = clk;
    SCE_FIFOEN   = fifo;
    memory_fence();

    SCE_RESETRAM = SCE_RESETRAM_CLEAR;
    memory_fence();
    return wait_ramclr();
}

int pke_wait_fr(volatile uint32_t *fr, uint32_t ms)
{
    uint64_t start = millis();
    while (!(*fr & 0x01u)) {
        if (millis() - start > ms)
            return PKE_ERR_TIMEOUT;
    }
    *fr |= 0x01u;
    memory_fence();
    return PKE_OK;
}

int pke_run(uint32_t fn, uint32_t ms)
{
    PKE_CRFUNC = fn & 0xFFu;
    PKE_FR = 0x0F;
    memory_fence();
    PKE_AR = 0x5A;
    memory_fence();
    return pke_wait_fr(&PKE_FR, ms);
}

/* Arbitrary modulus (the vendor's "RSA" path) */

typedef struct {
    uint32_t n_words;
    uint32_t pack_bytes;
} mod_ctx_t;

static uint32_t even_words(uint32_t w)
{
    return (w + 1u) & ~1u;
}

/* Workaround the vendor runs for an accelerator bug: one dummy multiply
 * at 6144 bits with a single multiplier, which clears its cache. */
static int mult_mod_ex(void)
{
    PKE_OPTNW = 6144u & 0x3FFFu;
    pke_seg_zero(PKE_SEG_PIB, 6144u / 32u);
    PKE_PIB0   = 0;
    PKE_PIB1   = 6144u / 32u;
    PKE_MIMMCR = PKE_MIMMCR_1MUL;
    PKE_POB    = 0;
    return pke_run(PKE_FN_RSAMM, PKE_MS_OP);
}

static int mod_check(const uint32_t *n, uint32_t n_words, uint32_t max_bits)
{
    if (n == NULL || n_words == 0 || n_words > PKE_MAX_WORDS || !aligned4(n))
        return PKE_ERR_ARG;
    uint32_t bits = pke_bn_bits(n, n_words);
    if (bits < 65 || bits > max_bits)
        return PKE_ERR_ARG;
    return PKE_OK;
}

static int operand_check(const uint32_t *x, uint32_t x_words, uint32_t limit_bits)
{
    if (x == NULL || x_words == 0 || x_words > PKE_MAX_WORDS || !aligned4(x))
        return PKE_ERR_ARG;
    if (pke_bn_bits(x, x_words) > limit_bits)
        return PKE_ERR_ARG;
    return PKE_OK;
}

static int mod_setup(mod_ctx_t *ctx, const uint32_t *n, uint32_t n_words,
                     bool advance, bool addsub)
{
    uint32_t bits = pke_bn_bits(n, n_words);
    bool fast = false;
    int rc;

    if ((n[0] & 1u) == 0)
        return PKE_ERR_ARG;

    rc = pke_sce_prepare(0);
    if (rc != PKE_OK)
        return rc;

    PKE_OPTLTX  = 0;
    PKE_OPTMASK = addsub ? 1u : 0u;

    ctx->n_words    = n_words;
    ctx->pack_bytes = 0;

    if (advance && bits >= 256 && bits <= 6144) {
        rc = mult_mod_ex();
        if (rc != PKE_OK)
            return rc;
        PKE_MIMMCR = PKE_MIMMCR_4MUL;
        fast = true;
    } else {
        PKE_MIMMCR = 0;
    }

    PKE_OPTNW = bits & 0x3FFFu;
    PKE_PCON  = 0;
    pke_seg_write(PKE_SEG_PCON, n, n_words);

    /* H = 2^bits, placed after n on a 64 bit boundary */
    volatile uint32_t *h = PKE_SEG_PCON + even_words(pke_bn_words(n, n_words));
    uint32_t i;
    for (i = 0; i < bits / 32u; i++)
        h[i] = 0;
    h[i]     = 1u << (bits % 32u);
    h[i + 1] = 0;

    rc = pke_run(PKE_FN_RSAINIT, PKE_MS_OP);
    if (rc != PKE_OK)
        return rc;

    if (fast && (bits % 256u) != 0) {
        uint32_t padded = bits + (256u - (bits % 256u));
        PKE_OPTNW = padded & 0x3FFFu;
        ctx->pack_bytes = padded / 8u - n_words * 4u;
    }
    return PKE_OK;
}

static int mod_addsub(uint32_t fn, const uint32_t *x, uint32_t x_words,
                      const uint32_t *y, uint32_t y_words,
                      const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    mod_ctx_t ctx;
    int rc;

    if ((rc = mod_check(n, n_words, 8191)) != PKE_OK)
        return rc;
    uint32_t nbits = pke_bn_bits(n, n_words);
    if ((rc = operand_check(x, x_words, nbits)) != PKE_OK)
        return rc;
    if ((rc = operand_check(y, y_words, nbits)) != PKE_OK)
        return rc;
    if (out == NULL || !aligned4(out))
        return PKE_ERR_ARG;

    if ((rc = mod_setup(&ctx, n, n_words, false, true)) != PKE_OK)
        return rc;

    pke_seg_zero(PKE_SEG_POB, PKE_POB_BYTES / 4u);
    pke_seg_zero(PKE_SEG_PIB, PKE_PIB_BYTES / 4u);
    pke_seg_write(PKE_SEG_PIB, x, x_words);
    PKE_PIB0 = 0;
    pke_seg_write(PKE_SEG_PIB + even_words(n_words), y, y_words);
    PKE_PIB1 = even_words(n_words);
    PKE_POB  = 0;

    if ((rc = pke_run(fn, PKE_MS_OP)) != PKE_OK)
        return rc;
    pke_seg_read(out, PKE_SEG_POB, n_words);
    return PKE_OK;
}

int pke_mod_add(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    return mod_addsub(PKE_FN_RSAMA, x, x_words, y, y_words, n, n_words, out);
}

int pke_mod_sub(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    return mod_addsub(PKE_FN_RSAMS, x, x_words, y, y_words, n, n_words, out);
}

int pke_mod_mul(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    mod_ctx_t ctx;
    int rc;

    if ((rc = mod_check(n, n_words, 8191)) != PKE_OK)
        return rc;
    uint32_t nbits = pke_bn_bits(n, n_words);
    if ((rc = operand_check(x, x_words, nbits)) != PKE_OK)
        return rc;
    if ((rc = operand_check(y, y_words, nbits)) != PKE_OK)
        return rc;
    if (out == NULL || !aligned4(out))
        return PKE_ERR_ARG;

    if ((rc = mod_setup(&ctx, n, n_words, true, false)) != PKE_OK)
        return rc;

    uint32_t y_at = ctx.pack_bytes / 4u + even_words(n_words);
    if (y_at + y_words > PKE_PIB_BYTES / 4u)
        return PKE_ERR_ARG;

    pke_seg_zero(PKE_SEG_PIB, PKE_PIB_BYTES / 4u);
    pke_seg_zero(PKE_SEG_POB, PKE_POB_BYTES / 4u);
    pke_seg_write(PKE_SEG_PIB, x, x_words);
    pke_seg_write(PKE_SEG_PIB + y_at, y, y_words);
    PKE_PIB0 = 0;
    PKE_PIB1 = y_at;
    PKE_POB  = 0;

    if ((rc = pke_run(PKE_FN_RSAMM, PKE_MS_OP)) != PKE_OK)
        return rc;
    pke_seg_read(out, PKE_SEG_POB, n_words);

    return mult_mod_ex();
}

int pke_mod_exp(const uint32_t *x, uint32_t x_words,
                const uint32_t *e, uint32_t e_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    mod_ctx_t ctx;
    int rc;

    if ((rc = mod_check(n, n_words, 8191)) != PKE_OK)
        return rc;
    uint32_t nbits = pke_bn_bits(n, n_words);
    if ((rc = operand_check(x, x_words, nbits)) != PKE_OK)
        return rc;
    if (e == NULL || e_words == 0 || e_words > PKE_MAX_WORDS || !aligned4(e))
        return PKE_ERR_ARG;
    if (out == NULL || !aligned4(out))
        return PKE_ERR_ARG;

    uint32_t ebits = pke_bn_bits(e, e_words);
    if (ebits > 8192)
        return PKE_ERR_ARG;
    if (ebits == 0) {
        /* x^0 = 1, as the vendor wrapper does without touching hardware */
        for (uint32_t i = 0; i < n_words; i++)
            out[i] = 0;
        out[0] = 1;
        return PKE_OK;
    }

    if ((rc = mod_setup(&ctx, n, n_words, true, false)) != PKE_OK)
        return rc;

    PKE_OPTEW = ebits & 0x3FFFu;
    pke_seg_zero(PKE_SEG_PIB, PKE_PIB_BYTES / 4u);
    pke_seg_zero(PKE_SEG_PKB, PKE_PKB_BYTES / 4u);
    pke_seg_zero(PKE_SEG_POB, PKE_POB_BYTES / 4u);
    pke_seg_write(PKE_SEG_PIB, x, x_words);
    PKE_PIB0 = 0;
    pke_seg_write(PKE_SEG_PKB, e, e_words);
    PKE_PKB = 0;
    PKE_POB = 0;

    if ((rc = pke_run(PKE_FN_RSAME, PKE_MS_EXP)) != PKE_OK)
        return rc;
    pke_seg_read(out, PKE_SEG_POB, n_words);
    return PKE_OK;
}

int pke_mod_inv(const uint32_t *x, uint32_t x_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out)
{
    int rc;

    if ((rc = mod_check(n, n_words, 4096)) != PKE_OK)
        return rc;
    if (n_words > PKE_INV_MAX_WORDS || x_words > PKE_INV_MAX_WORDS)
        return PKE_ERR_ARG;
    uint32_t nbits = pke_bn_bits(n, n_words);
    if ((rc = operand_check(x, x_words, nbits)) != PKE_OK)
        return rc;
    if (pke_bn_bits(x, x_words) == 0)
        return PKE_ERR_NOT_COPRIME;
    if (out == NULL || !aligned4(out))
        return PKE_ERR_ARG;

    if ((rc = pke_sce_prepare(0)) != PKE_OK)
        return rc;
    PKE_OPTLTX  = 0;
    PKE_OPTMASK = 0;
    PKE_MIMMCR  = 0;

    PKE_OPTNW = nbits & 0x3FFFu;
    PKE_PCON  = 0;
    pke_seg_zero(PKE_SEG_PCON, n_words + 8u);
    pke_seg_write(PKE_SEG_PCON, n, n_words);
    if ((rc = pke_run(PKE_FN_INVINIT, PKE_MS_OP)) != PKE_OK)
        return rc;

    pke_seg_zero(PKE_SEG_PIB, PKE_PIB_BYTES / 4u);
    pke_seg_zero(PKE_SEG_POB, PKE_POB_BYTES / 4u);
    pke_seg_write(PKE_SEG_PIB, x, x_words);
    PKE_PIB0 = 0;
    PKE_POB  = 0;
    if ((rc = pke_run(PKE_FN_MODINV, PKE_MS_OP)) != PKE_OK)
        return rc;

    /* The inverse is ready when SRMFSM bit 8 sets. When x and n share a
     * factor it never sets, which is how the vendor detects that case. */
    uint64_t start = millis();
    while (!(PKE_SRMFSM & 0x100u)) {
        if (millis() - start > PKE_MS_INV)
            return PKE_ERR_NOT_COPRIME;
    }
    PKE_SRMFSM |= 0x100u;
    memory_fence();

    pke_seg_read(out, PKE_SEG_POB, n_words);
    return PKE_OK;
}

int pke_gcd(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words, uint32_t *out)
{
    int rc;

    if (x_words > PKE_INV_MAX_WORDS || y_words > PKE_INV_MAX_WORDS)
        return PKE_ERR_ARG;
    if ((rc = operand_check(x, x_words, 4096)) != PKE_OK)
        return rc;
    if ((rc = operand_check(y, y_words, 4096)) != PKE_OK)
        return rc;
    if (pke_bn_bits(x, x_words) == 0 || pke_bn_bits(y, y_words) == 0)
        return PKE_ERR_ARG;
    if (out == NULL || !aligned4(out))
        return PKE_ERR_ARG;

    uint32_t d = even_words(x_words > y_words ? x_words : y_words);

    if ((rc = pke_sce_prepare(0)) != PKE_OK)
        return rc;
    PKE_OPTLTX  = 0;
    PKE_OPTMASK = 0;
    PKE_MIMMCR  = 0;

    PKE_OPTNW = (d * 32u) & 0x3FFFu;
    pke_seg_zero(PKE_SEG_PIB, PKE_PIB_BYTES / 4u);
    pke_seg_zero(PKE_SEG_POB, PKE_POB_BYTES / 4u);
    pke_seg_write(PKE_SEG_PIB, x, x_words);
    PKE_PIB0 = 0;
    pke_seg_write(PKE_SEG_PIB + d, y, y_words);
    PKE_PIB1 = d;
    PKE_POB  = 0;

    if ((rc = pke_run(PKE_FN_GCD, PKE_MS_OP)) != PKE_OK)
        return rc;
    pke_seg_read(out, PKE_SEG_POB, d);
    return PKE_OK;
}
