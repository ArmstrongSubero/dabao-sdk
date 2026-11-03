/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * SCE (Secure Crypto Engine) register definitions.
 * Covers AES, SHA/Hash, TRNG, PKE, ALU and the SCE internal DMA channel.
 *
 * The PKE, ALU and SDMA definitions follow the CrossBar Daric SDK
 * (drivers/Inc/sce/auth.h, Apache 2.0). The Baochip-1x carries the same
 * SCE, and the AES, HASH, TRNG and control blocks at these offsets have
 * been run on Dabao hardware; PKE and ALU have not yet.
 */

#ifndef _HARDWARE_REGS_SCE_H
#define _HARDWARE_REGS_SCE_H

#include "hardware/regs/addressmap.h"

/* SCE control registers */
#define SCE_SCEMODE         REG32(SCE_CTRL_BASE + 0x00)
#define SCE_SUBCLKEN        REG32(SCE_CTRL_BASE + 0x04)
#define SCE_BUSYSR          REG32(SCE_CTRL_BASE + 0x10)
#define SCE_DONEFR          REG32(SCE_CTRL_BASE + 0x14)
#define SCE_ERRFR           REG32(SCE_CTRL_BASE + 0x18)
#define SCE_RESETRAM        REG32(SCE_CTRL_BASE + 0x1C)
#define SCE_FIFOEN          REG32(SCE_CTRL_BASE + 0x30)
#define SCE_FIFOCLR         REG32(SCE_CTRL_BASE + 0x34)
#define SCE_FIFOCNT5        REG32(SCE_CTRL_BASE + 0x54)

/* SUBCLKEN bits */
#define SCE_CLK_SDMA        (1u << 0)
#define SCE_CLK_AES         (1u << 1)
#define SCE_CLK_PKE         (1u << 2)
#define SCE_CLK_HASH        (1u << 3)
#define SCE_CLK_ALU         (1u << 4)

/* BUSYSR and DONEFR bits */
#define SCE_BUSY_RAMCLR     (1u << 8)
#define SCE_DONE_ICH        (1u << 5)

/* RESETRAM commands */
#define SCE_RESETRAM_CTRL   0x5Au   /* reset the SCE control state */
#define SCE_RESETRAM_CLEAR  0xA5u   /* zero the SCE RAM */

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

/* SCE internal DMA channel (ICH): copies between SCE RAM segments only,
 * it has no path to the system bus. */
#define SDMA_START          REG32(SCE_SDMA_BASE + 0x00)
#define SDMA_ICH_OPT        REG32(SCE_SDMA_BASE + 0x50)
#define SDMA_ICH_SEGID      REG32(SCE_SDMA_BASE + 0x54)
#define SDMA_ICH_ADDR_R     REG32(SCE_SDMA_BASE + 0x58)
#define SDMA_ICH_ADDR_W     REG32(SCE_SDMA_BASE + 0x5C)
#define SDMA_ICH_SIZE       REG32(SCE_SDMA_BASE + 0x60)
#define SDMA_ICH_START      0x5Au

/* PKE registers */
#define PKE_CRFUNC          REG32(SCE_PKE_BASE + 0x00)
#define PKE_AR              REG32(SCE_PKE_BASE + 0x04)
#define PKE_SRMFSM          REG32(SCE_PKE_BASE + 0x08)
#define PKE_FR              REG32(SCE_PKE_BASE + 0x0C)
#define PKE_OPTNW           REG32(SCE_PKE_BASE + 0x10)
#define PKE_OPTEW           REG32(SCE_PKE_BASE + 0x14)
#define PKE_OPTRW           REG32(SCE_PKE_BASE + 0x18)
#define PKE_OPTLTX          REG32(SCE_PKE_BASE + 0x1C)
#define PKE_OPTMASK         REG32(SCE_PKE_BASE + 0x20)
#define PKE_MIMMCR          REG32(SCE_PKE_BASE + 0x24)
#define PKE_PCON            REG32(SCE_PKE_BASE + 0x30)
#define PKE_PIB0            REG32(SCE_PKE_BASE + 0x34)
#define PKE_PIB1            REG32(SCE_PKE_BASE + 0x38)
#define PKE_PKB             REG32(SCE_PKE_BASE + 0x3C)
#define PKE_POB             REG32(SCE_PKE_BASE + 0x40)

/* PKE function codes */
#define PKE_FN_ECINIT       0x01
#define PKE_FN_ECMM         0x02
#define PKE_FN_ECI2MA       0x03
#define PKE_FN_ECI2MD       0x04
#define PKE_FN_ECPA         0x05
#define PKE_FN_ECPD         0x06
#define PKE_FN_ECM2I        0x07
#define PKE_FN_ECINV        0x08
#define PKE_FN_ECMA         0x09
#define PKE_FN_ECMS         0x0A
#define PKE_FN_ECPM         0x0B
#define PKE_FN_RSAINIT      0x11
#define PKE_FN_RSAMM        0x12
#define PKE_FN_RSAME        0x13
#define PKE_FN_MODINV       0x18
#define PKE_FN_RSAMA        0x19
#define PKE_FN_RSAMS        0x1A
#define PKE_FN_EDI2MA       0x23
#define PKE_FN_EDI2MD       0x24
#define PKE_FN_EDPA         0x25
#define PKE_FN_EDPD         0x26
#define PKE_FN_EDM2I        0x27
#define PKE_FN_EDPM         0x2B
#define PKE_FN_X25519       0x3B
#define PKE_FN_GCD          0x51
#define PKE_FN_MODL         0x81
#define PKE_FN_INVINIT      0xFE

#define PKE_MIMMCR_4MUL     0x103u
#define PKE_MIMMCR_1MUL     0x100u

/* PKE segments in SCE RAM. PCON and PKB share 0x0800, PIB and PSIB
 * together make the 2 KB input area. */
#define PKE_SEG_PCON        ((volatile uint32_t *)(SCE_BASE + 0x0800))
#define PKE_SEG_PKB         ((volatile uint32_t *)(SCE_BASE + 0x0800))
#define PKE_SEG_PIB         ((volatile uint32_t *)(SCE_BASE + 0x0C00))
#define PKE_SEG_PSIB        ((volatile uint32_t *)(SCE_BASE + 0x1000))
#define PKE_SEG_POB         ((volatile uint32_t *)(SCE_BASE + 0x1400))
#define PKE_SEG_PSOB        ((volatile uint32_t *)(SCE_BASE + 0x1800))
#define PKE_PCON_BYTES      1024u
#define PKE_PKB_BYTES       1024u
#define PKE_PIB_BYTES       2048u
#define PKE_POB_BYTES       1024u
#define PKE_PSOB_BYTES      1024u

/* Segment ids used by the ALU pointers and the ICH channel */
#define SCE_SEGID_PIB       9u
#define SCE_SEGID_POB       11u
#define SCE_SEGID_PSOB      12u

/* ALU registers */
#define ALU_CRFUNC          REG32(SCE_ALU_BASE + 0x00)
#define ALU_AR              REG32(SCE_ALU_BASE + 0x04)
#define ALU_SRMFSM          REG32(SCE_ALU_BASE + 0x08)
#define ALU_FR              REG32(SCE_ALU_BASE + 0x0C)
#define ALU_CRDIVLEN        REG32(SCE_ALU_BASE + 0x10)
#define ALU_SRDIVLEN        REG32(SCE_ALU_BASE + 0x14)
#define ALU_OPT             REG32(SCE_ALU_BASE + 0x18)
#define ALU_OPTLTX          REG32(SCE_ALU_BASE + 0x1C)
#define ALU_SEGPTR_DE       REG32(SCE_ALU_BASE + 0x30)
#define ALU_SEGPTR_DS       REG32(SCE_ALU_BASE + 0x34)
#define ALU_SEGPTR_QT       REG32(SCE_ALU_BASE + 0x38)
#define ALU_SEGPTR_RM       REG32(SCE_ALU_BASE + 0x3C)

/* ALU function codes */
#define ALU_FN_DIV          0x01
#define ALU_FN_ADD          0x02
#define ALU_FN_SUB          0x03
#define ALU_FN_SFT          0x10
#define ALU_FN_SFTW         0x11
#define ALU_FN_BLG          0x20
#define ALU_FN_BEX          0x30

/* ALU OPT fields */
#define ALU_OPT_BEX_WD64    (1u << 19)
#define ALU_OPT_BEX_WD32    (1u << 18)
#define ALU_OPT_BEX_BYTE    (1u << 17)
#define ALU_OPT_BEX_BIT     (1u << 16)
#define ALU_OPT_SFT_LEFT    (1u << 2)
#define ALU_OPT_BLG_TRUE11  (1u << 7)
#define ALU_OPT_BLG_TRUE01  (1u << 6)
#define ALU_OPT_BLG_TRUE10  (1u << 5)
#define ALU_OPT_BLG_TRUE00  (1u << 4)

#define ALU_SEGPTR(seg, off) ((((uint32_t)(seg) & 0xFFu) << 12) | ((uint32_t)(off) & 0xFFFu))

/* FIFO-5 helpers (TRNG output) */
#define FIFO5_ENABLE()      (SCE_FIFOEN |= 0x20)
#define FIFO5_DISABLE()     (SCE_FIFOEN &= ~0x20)
#define FIFO5_CLEAR()       (SCE_FIFOCLR = 0x0000FF05)
#define FIFO5_COUNT()       ((SCE_FIFOCNT5 >> 4) & 0x0FFF)

#endif
