/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * ALU (big number arithmetic unit) driver for the Baochip-1x SCE.
 *
 * Ported from the CrossBar Daric SDK drivers/Src/sce/alu.c and the
 * division wrapper in middleware/crossbar/crypto/ll_api.c (Copyright
 * 2024 to 2026 CrossBar, Inc., Apache 2.0), with bounded waits, argument
 * checks and status returns added.
 *
 * Division, addition and subtraction take numbers least significant word
 * first. The shift, logic and byte exchange operations take them most
 * significant word first, which is how the vendor drives the ALU for
 * those (endian register set to big in, big out).
 *
 * The ALU uses the PKE input and output segments of SCE RAM; it does not
 * clear the rest of SCE RAM, so an AES key survives an ALU call.
 *
 * The alu_mod_ functions and alu_gcd do modular arithmetic for any
 * modulus up to 2048 bits. Reductions and divisions run on the ALU, the
 * multiplications and the Montgomery loop for an odd modulus run on the
 * CPU. They do not use the PKE.
 */

#ifndef _HARDWARE_ALU_H
#define _HARDWARE_ALU_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ALU_OK           0
#define ALU_ERR_ARG    (-1)
#define ALU_ERR_TIMEOUT (-2)
#define ALU_ERR_NOT_COPRIME (-3)

/* Largest modulus for the alu_mod_ functions, in 32 bit words */
#define ALU_MOD_MAX_WORDS 64u

/* Truth table bits for alu_logic: output 1 where the input pair is set */
#define ALU_LOGIC_11    (1u << 7)
#define ALU_LOGIC_01    (1u << 6)
#define ALU_LOGIC_10    (1u << 5)
#define ALU_LOGIC_00    (1u << 4)
#define ALU_AND         (ALU_LOGIC_11)
#define ALU_OR          (ALU_LOGIC_11 | ALU_LOGIC_01 | ALU_LOGIC_10)
#define ALU_XOR         (ALU_LOGIC_01 | ALU_LOGIC_10)

/* q = x / y, r = x mod y. Up to 256 words each, x below 2^8191, y non
 * zero. q must hold x_words rounded up to even, r must hold y_words
 * rounded up to even. *q_words and *r_words get the lengths written.
 * q or r (with its length pointer) may be NULL, not both. */
int alu_div(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words,
            uint32_t *q, uint32_t *q_words,
            uint32_t *r, uint32_t *r_words);

/* sum = x + y, diff = x minus y. Even word counts up to 256, values
 * below 2^8191. The result is max(x_words, y_words) words. carry and
 * borrow may be NULL. For alu_sub, x_words must be at least y_words. */
int alu_add(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words,
            uint32_t *sum, uint32_t *carry);
int alu_sub(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words,
            uint32_t *diff, uint32_t *borrow);

/* Most significant word first. out holds words rounded up to even.
 * bits and count are 0 to 255. */
int alu_shift_right(const uint32_t *in, uint32_t words, uint32_t bits,
                    uint32_t *out);
int alu_shift_right_words(const uint32_t *in, uint32_t words, uint32_t count,
                          uint32_t *out);

/* Bitwise logic, most significant word first. op is built from the
 * ALU_LOGIC_ bits. out holds max(x_words, y_words) rounded up to even. */
int alu_logic(const uint32_t *x, uint32_t x_words,
              const uint32_t *y, uint32_t y_words,
              uint32_t op, uint32_t *out);

/* Byte exchange of up to 252 words, as the vendor's alu_bex. out holds
 * words rounded up to even. */
int alu_byte_exchange(const uint32_t *in, uint32_t words, uint32_t *out);

/* Modular arithmetic, least significant word first. Every operand is up
 * to ALU_MOD_MAX_WORDS words, n is non zero, and out holds n_words words.
 * x and y may be any size up to that limit; they are reduced mod n first. */
int alu_mod_add(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);
int alu_mod_sub(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);
int alu_mod_mul(const uint32_t *x, uint32_t x_words,
                const uint32_t *y, uint32_t y_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);

/* out = x ^ e mod n */
int alu_mod_exp(const uint32_t *x, uint32_t x_words,
                const uint32_t *e, uint32_t e_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);

/* out = 1 / x mod n. Returns ALU_ERR_NOT_COPRIME if no inverse exists. */
int alu_mod_inv(const uint32_t *x, uint32_t x_words,
                const uint32_t *n, uint32_t n_words, uint32_t *out);

/* out = gcd(x, y), written as max(x_words, y_words) words. */
int alu_gcd(const uint32_t *x, uint32_t x_words,
            const uint32_t *y, uint32_t y_words, uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif
