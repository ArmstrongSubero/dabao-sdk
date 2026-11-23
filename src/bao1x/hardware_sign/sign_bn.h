/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Small helpers shared by ecdsa.c and ed25519.c: word array compare and
 * subtract on the CPU, byte order conversion and wiping. Big numbers are
 * arrays of 32 bit words, least significant word first, as the PKE and
 * ALU drivers take them. Internal to hardware_sign.
 */

#ifndef _HARDWARE_SIGN_BN_H
#define _HARDWARE_SIGN_BN_H

#include <stdint.h>
#include <stdbool.h>
#include "hardware/pke.h"
#include "hardware/sign.h"

/* Largest number of words any value here needs: a P-384 coordinate or
 * scalar is 12, a SHA-512 digest read as a number 16. */
#define SIGN_MAX_WORDS 16u

/* Zero a buffer in a way the compiler cannot drop as a dead store. */
static inline void sign_wipe(void *p, uint32_t n)
{
    volatile uint8_t *v = (volatile uint8_t *)p;
    while (n--)
        *v++ = 0;
}

static inline bool sign_is_zero(const uint32_t *a, uint32_t w)
{
    uint32_t acc = 0;
    for (uint32_t i = 0; i < w; i++)
        acc |= a[i];
    return acc == 0;
}

/* -1, 0 or 1 as a is below, equal to or above b */
static inline int sign_cmp(const uint32_t *a, const uint32_t *b, uint32_t w)
{
    for (uint32_t i = w; i > 0; i--) {
        if (a[i - 1] != b[i - 1])
            return a[i - 1] < b[i - 1] ? -1 : 1;
    }
    return 0;
}

/* r = a minus b, returns the borrow. r may be a or b. */
static inline uint32_t sign_sub(uint32_t *r, const uint32_t *a, const uint32_t *b, uint32_t w)
{
    uint32_t borrow = 0;
    for (uint32_t i = 0; i < w; i++) {
        uint64_t d = (uint64_t)a[i] - b[i] - borrow;
        r[i] = (uint32_t)d;
        borrow = (uint32_t)(d >> 32) & 1u;
    }
    return borrow;
}

/* Bring a below m by subtracting m while it is not. Every caller passes a
 * value below 2m, or a modular result that should already be reduced, so
 * the loop runs at most once in practice; the bound only guards against
 * an engine that returned garbage. */
static inline void sign_fix(uint32_t *a, const uint32_t *m, uint32_t w)
{
    for (uint32_t i = 0; i < 4u && sign_cmp(a, m, w) >= 0; i++)
        sign_sub(a, a, m, w);
}

/* Big endian bytes (len = 4 * w) to words, and back */
static inline void sign_be_to_words(uint32_t *w, const uint8_t *b, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        const uint8_t *p = b + 4u * (words - 1u - i);
        w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }
}

static inline void sign_words_to_be(uint8_t *b, const uint32_t *w, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        uint8_t *p = b + 4u * (words - 1u - i);
        p[0] = (uint8_t)(w[i] >> 24);
        p[1] = (uint8_t)(w[i] >> 16);
        p[2] = (uint8_t)(w[i] >> 8);
        p[3] = (uint8_t)w[i];
    }
}

/* Little endian bytes (len = 4 * w) to words, and back */
static inline void sign_le_to_words(uint32_t *w, const uint8_t *b, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        const uint8_t *p = b + 4u * i;
        w[i] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
               ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    }
}

static inline void sign_words_to_le(uint8_t *b, const uint32_t *w, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        uint8_t *p = b + 4u * i;
        p[0] = (uint8_t)w[i];
        p[1] = (uint8_t)(w[i] >> 8);
        p[2] = (uint8_t)(w[i] >> 16);
        p[3] = (uint8_t)(w[i] >> 24);
    }
}

/* Constant pattern compare of two byte strings; true when equal */
static inline bool sign_same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint8_t acc = 0;
    for (uint32_t i = 0; i < n; i++)
        acc |= a[i] ^ b[i];
    return acc == 0;
}

/*
 * Wipe the stack below the caller's frame, where the frames of the driver
 * calls just made (HMAC contexts, SHA blocks, words read back from the
 * SCE) were left. bytes must stay below the stack still free; the callers
 * pass SIGN_SCRUB_BYTES, which covers the deepest call chain here (see
 * CHANGES.md for the measured depths) and fits the 8 KB main stack and
 * the 16 KB machine mode stack of the BAO_MMU build.
 */
#define SIGN_SCRUB_BYTES 2048u

static void __attribute__((noinline, unused)) sign_scrub_stack(void)
{
    uint8_t buf[SIGN_SCRUB_BYTES];
    volatile uint8_t *p = buf;
    for (uint32_t i = 0; i < SIGN_SCRUB_BYTES; i++)
        p[i] = 0;
    /* tell the compiler the zeros are read, so the stores stay */
    __asm__ volatile ("" : : "r"(buf) : "memory");
}

/* Modular arithmetic on the PKE. Every operand is already below m. The
 * result is brought below m on the CPU as well, in case the engine leaves
 * it in [m, 2m). */

static inline int m_mul(uint32_t *out, const uint32_t *x, const uint32_t *y,
                 const uint32_t *m, uint32_t w)
{
    if (pke_mod_mul(x, w, y, w, m, w, out) != PKE_OK)
        return SIGN_ERR_HW;
    sign_fix(out, m, w);
    return SIGN_OK;
}

static inline int m_add(uint32_t *out, const uint32_t *x, const uint32_t *y,
                 const uint32_t *m, uint32_t w)
{
    if (pke_mod_add(x, w, y, w, m, w, out) != PKE_OK)
        return SIGN_ERR_HW;
    sign_fix(out, m, w);
    return SIGN_OK;
}

static inline int m_sub(uint32_t *out, const uint32_t *x, const uint32_t *y,
                 const uint32_t *m, uint32_t w)
{
    if (pke_mod_sub(x, w, y, w, m, w, out) != PKE_OK)
        return SIGN_ERR_HW;
    sign_fix(out, m, w);
    return SIGN_OK;
}

static inline int m_inv(uint32_t *out, const uint32_t *x, const uint32_t *m, uint32_t w)
{
    if (pke_mod_inv(x, w, m, w, out) != PKE_OK)
        return SIGN_ERR_HW;
    sign_fix(out, m, w);
    return SIGN_OK;
}

/* Clear the SCE RAM (the PKE driver's reset and RAM clear) and the stack
 * the driver calls used. Called by everything that handled a secret. */
static inline int wipe_engine(int rc)
{
    if (pke_sce_prepare(0) != PKE_OK && rc == SIGN_OK)
        rc = SIGN_ERR_HW;
    sign_scrub_stack();
    return rc;
}

#endif
