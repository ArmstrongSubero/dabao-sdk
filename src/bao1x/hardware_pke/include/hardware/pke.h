/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * PKE (public key engine) driver for the Baochip-1x SCE.
 *
 * Ported from the CrossBar Daric SDK (drivers/Src/sce/pke_rsa.c and
 * pke_curve.c, Copyright 2024 to 2026 CrossBar, Inc., Apache 2.0). The
 * register sequences are the vendor's; what changed for the Baochip is
 * that every wait is bounded, every argument is checked and every call
 * returns a status instead of asserting.
 *
 * Big numbers are arrays of 32 bit words, least significant word first.
 * Curve points are x then y, each coordinate in that same word order.
 *
 * Every call starts by resetting the SCE control state and clearing the
 * SCE RAM, as the vendor library does before each PKE operation. That
 * also clears the AES key, so call aes_set_key() again after any PKE call.
 * Every SCE block clock is left on and the FIFO enables are restored.
 *
 * The first call sets up the PKE clock (see pke_clock_init below). boot1
 * leaves the PKE on the raw 2 GHz PLL1 output, where it stalls or gives
 * wrong answers.
 */

#ifndef _HARDWARE_PKE_H
#define _HARDWARE_PKE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PKE_OK               0
#define PKE_ERR_ARG        (-1)   /* bad pointer, length or value       */
#define PKE_ERR_TIMEOUT    (-2)   /* engine never reported done         */
#define PKE_ERR_NOT_COPRIME (-3)  /* modular inverse does not exist     */

/* Modular arithmetic on an arbitrary odd modulus n of 65 to 8191 bits.
 * x and y must be below 2^bits(n). Results are n_words long. Lengths
 * are in 32 bit words, at most 256. */
int pke_mod_add(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);
int pke_mod_sub(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);
int pke_mod_mul(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);
int pke_mod_exp(const uint32_t *x, uint32_t x_words,
                const uint32_t *e, uint32_t e_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);

/* out = 1 / x mod n. n may be odd or even, 65 to 4096 bits, x at most
 * 128 words. Returns PKE_ERR_NOT_COPRIME if no inverse exists. */
int pke_mod_inv(const uint32_t *x, uint32_t x_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);

/* out = gcd(x, y). Both non zero, at most 4096 bits and 128 words.
 * out must hold max(x_words, y_words) rounded up to an even count. */
int pke_gcd(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words, uint32_t *out);

/* Elliptic curves in short Weierstrass form, with the vendor's fixed
 * parameters. The value is the vendor's curve code. */
typedef enum {
    PKE_SECP192K1 = 0x00,
    PKE_SECP192R1 = 0x80,
    PKE_SECP224K1 = 0x01,
    PKE_SECP224R1 = 0x81,
    PKE_SECP256K1 = 0x02,
    PKE_SECP256R1 = 0x82,
    PKE_SECP384R1 = 0x83,
    PKE_SECP521R1 = 0x84,
} pke_curve_t;

/* Words in one coordinate (and in the scalar k): 6, 7, 8, 12 or 17. */
uint32_t pke_ecc_words(pke_curve_t curve);

/* The curve's base point G, x then y, or NULL for an unknown curve. */
const uint32_t *pke_ecc_base_point(pke_curve_t curve);

/* out = k * p, out = k * G, out = a + b, out = 2 * p. k is
 * pke_ecc_words(curve) words. k must not be zero. Points hold 2 *
 * pke_ecc_words(curve) words. pke_ecc_point_add handles a == b. */
int pke_ecc_point_mul(pke_curve_t curve, const uint32_t *k,
                      const uint32_t *p, uint32_t *out);
int pke_ecc_point_mul_base(pke_curve_t curve, const uint32_t *k, uint32_t *out);
int pke_ecc_point_add(pke_curve_t curve, const uint32_t *a,
                      const uint32_t *b, uint32_t *out);
int pke_ecc_point_double(pke_curve_t curve, const uint32_t *p, uint32_t *out);

/* X25519 (RFC 7748). Scalars and u coordinates are 32 bytes, little
 * endian, as the RFC writes them. pke_x25519 uses k exactly as given;
 * the other two clamp the private key first. */
int pke_x25519(const uint8_t k[32], const uint8_t u[32], uint8_t out[32]);
int pke_x25519_public(const uint8_t priv[32], uint8_t pub[32]);
int pke_x25519_shared(const uint8_t priv[32], const uint8_t peer_pub[32],
                      uint8_t shared[32]);

/* Ed25519 curve points in affine form, coordinates as 32 little endian
 * bytes. These are the raw point operations, not signing. */
typedef struct {
    uint8_t x[32];
    uint8_t y[32];
} pke_ed_point_t;

int pke_ed25519_point_mul(const uint8_t k[32], const pke_ed_point_t *p,
                          pke_ed_point_t *out);
int pke_ed25519_point_mul_base(const uint8_t k[32], pke_ed_point_t *out);
int pke_ed25519_point_add(const pke_ed_point_t *a, const pke_ed_point_t *b,
                          pke_ed_point_t *out);
int pke_ed25519_point_double(const pke_ed_point_t *p, pke_ed_point_t *out);

/* PKE clock. pke_clock_init sets PLL1 to VCO / 12 (about 174 MHz) with
 * PLL0 and the CPU clock unchanged, clears PKE clock skipping and sets the
 * PKE divider to 0xFF. The first PKE call runs it if nothing has yet.
 * pke_set_clock_divider changes only the divider; the PKE clock is PLL1
 * times (fd + 1) / 256. pke_clock_status reads the PLL settings and the CGU
 * frequency sensors (values in MHz). All of it resets on power up. */
typedef struct {
    uint32_t pllmn;
    uint32_t pllq;
    uint32_t cgusec;
    uint32_t cgusel0;
    uint32_t fdpke;
    uint32_t fsvld;
    uint32_t fclk_mhz;
    uint32_t pke_mhz;
    uint32_t pll0_mhz;
    uint32_t pll1_mhz;
} pke_clock_status_t;

void pke_clock_init(void);
void pke_set_clock_divider(uint32_t fd);
void pke_clock_status(pke_clock_status_t *s);

/* Shared with alu.c */
int  pke_sce_prepare(uint32_t extra_clocks);
int  pke_wait_fr(volatile uint32_t *fr, uint32_t ms);
int  pke_run(uint32_t fn, uint32_t ms);
void pke_seg_write(volatile uint32_t *seg, const uint32_t *src, uint32_t words);
void pke_seg_zero(volatile uint32_t *seg, uint32_t words);
void pke_seg_read(uint32_t *dst, const volatile uint32_t *seg, uint32_t words);
uint32_t pke_bn_words(const uint32_t *x, uint32_t words);
uint32_t pke_bn_bits(const uint32_t *x, uint32_t words);

#ifdef __cplusplus
}
#endif

#endif
