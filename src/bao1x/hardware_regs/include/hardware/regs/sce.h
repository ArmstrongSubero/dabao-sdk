/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * SCE (Secure Crypto Engine) register definitions.
 * Covers AES, SHA/Hash, and TRNG peripherals.
 */

#ifndef _HARDWARE_REGS_SCE_H
#define _HARDWARE_REGS_SCE_H

#include "hardware/regs/addressmap.h"

/* SCE control registers */
#define SCE_SCEMODE         REG32(SCE_CTRL_BASE + 0x00)
#define SCE_SUBCLKEN        REG32(SCE_CTRL_BASE + 0x04)
#define SCE_BUSYSR          REG32(SCE_CTRL_BASE + 0x10)
#define SCE_FIFOEN          REG32(SCE_CTRL_BASE + 0x30)
#define SCE_FIFOCLR         REG32(SCE_CTRL_BASE + 0x34)
#define SCE_FIFOCNT5        REG32(SCE_CTRL_BASE + 0x54)

/* AES registers */
#define AES_CRFUNC          REG32(SCE_AES_BASE + 0x00)
#define AES_AR              REG32(SCE_AES_BASE + 0x04)
#define AES_SRMFSM          REG32(SCE_AES_BASE + 0x08)
#define AES_FR              REG32(SCE_AES_BASE + 0x0C)
#define AES_OPT1            REG32(SCE_AES_BASE + 0x10)
#define AES_OPT2            REG32(SCE_AES_BASE + 0x14)
#define AES_OPT3            REG32(SCE_AES_BASE + 0x18)
#define AES_KEY_BUF         ((volatile uint32_t *)(SCE_BASE + 0x1C00))
#define AES_IN_BUF          ((volatile uint32_t *)(SCE_BASE + 0x1D00))
#define AES_OUT_BUF         ((volatile uint32_t *)(SCE_BASE + 0x1E00))

/* AES function codes */
#define AES_FUNC_KS         0x00    /* key schedule */
#define AES_FUNC_ENC        0x01    /* encrypt */
#define AES_FUNC_DEC        0x02    /* decrypt */

/* Hash engine registers */
#define HASH_CRFUNC         REG32(SCE_HASH_BASE + 0x00)
#define HASH_AR             REG32(SCE_HASH_BASE + 0x04)
#define HASH_SRMFSM         REG32(SCE_HASH_BASE + 0x08)
#define HASH_FR             REG32(SCE_HASH_BASE + 0x0C)
#define HASH_OPT1           REG32(SCE_HASH_BASE + 0x10)
#define HASH_OPT2           REG32(SCE_HASH_BASE + 0x14)
#define HASH_OPT3           REG32(SCE_HASH_BASE + 0x18)
#define HASH_MSG_REG        REG32(SCE_HASH_BASE + 0x30)
#define HASH_HOUT_REG       REG32(SCE_HASH_BASE + 0x34)
#define HASH_LKEY_BUF       ((volatile uint32_t *)(SCE_BASE + 0x0000))
#define HASH_MSG_BUF        ((volatile uint32_t *)(SCE_BASE + 0x0400))
#define HASH_OUT_BUF        ((volatile uint32_t *)(SCE_BASE + 0x0600))

/* Hash function codes */
#define HASH_FUNC_SHA256    0x00
#define HASH_FUNC_INIT      0xFF

/* TRNG registers */
#define TRNG_CRSRC          REG32(SCE_TRNG_BASE + 0x00)
#define TRNG_CRANA          REG32(SCE_TRNG_BASE + 0x04)
#define TRNG_POSTPROC       REG32(SCE_TRNG_BASE + 0x08)
#define TRNG_OPT            REG32(SCE_TRNG_BASE + 0x0C)
#define TRNG_SR             REG32(SCE_TRNG_BASE + 0x10)
#define TRNG_AR             REG32(SCE_TRNG_BASE + 0x14)
#define TRNG_FR             REG32(SCE_TRNG_BASE + 0x18)
#define TRNG_CHAIN0         REG32(SCE_TRNG_BASE + 0x40)
#define TRNG_CHAIN1         REG32(SCE_TRNG_BASE + 0x44)
#define RNG_BUF_B           ((volatile uint32_t *)(SCE_BASE + 0x2300))

/* FIFO-5 helpers (TRNG output) */
#define FIFO5_ENABLE()      (SCE_FIFOEN |= 0x20)
#define FIFO5_DISABLE()     (SCE_FIFOEN &= ~0x20)
#define FIFO5_CLEAR()       (SCE_FIFOCLR = 0x0000FF05)
#define FIFO5_COUNT()       ((SCE_FIFOCNT5 >> 4) & 0x0FFF)

#endif
