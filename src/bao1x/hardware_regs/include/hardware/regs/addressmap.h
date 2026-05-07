/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Baochip-1x SoC address map.
 * All addresses verified against bao1x_peri.h and working bare metal code.
 */

#ifndef _HARDWARE_REGS_ADDRESSMAP_H
#define _HARDWARE_REGS_ADDRESSMAP_H

/* Memory */
#define RRAM_BASE               0x60000000UL
#define SRAM_BASE               0x61000000UL
#define IFRAM0_BASE             0x50000000UL
#define IFRAM1_BASE             0x50020000UL
#define XIP_BASE                0x70000000UL

/* UDMA peripherals */
#define UDMA_CTRL_BASE          0x50100000UL
#define UDMA_UART0_BASE         0x50101000UL
#define UDMA_UART1_BASE         0x50102000UL
#define UDMA_UART2_BASE         0x50103000UL
#define UDMA_UART3_BASE         0x50104000UL
#define UDMA_SPIM0_BASE         0x50105000UL
#define UDMA_SPIM1_BASE         0x50106000UL
#define UDMA_SPIM2_BASE         0x50107000UL
#define UDMA_SPIM3_BASE         0x50108000UL
#define UDMA_I2C0_BASE          0x50109000UL
#define UDMA_I2C1_BASE          0x5010A000UL
#define UDMA_I2C2_BASE          0x5010B000UL
#define UDMA_I2C3_BASE          0x5010C000UL
#define UDMA_SDIO_BASE          0x5010D000UL
#define UDMA_ADC_BASE           0x50114000UL

/* PWM, IOX, BIO */
#define PWM_BASE                0x50120000UL
#define BIO_BDMA_BASE           0x50124000UL
#define IOX_BASE                0x5012F000UL

/* BIO instruction memory */
#define BIO_IMEM0_BASE          0x50125000UL
#define BIO_IMEM1_BASE          0x50126000UL
#define BIO_IMEM2_BASE          0x50127000UL
#define BIO_IMEM3_BASE          0x50128000UL

/* Clock, power, always-on */
#define CGU_BASE                0x40040000UL
#define AON_BASE                0x40060000UL
#define RTC_BASE                0x40061000UL
#define WDT_BASE                0x40041000UL

/* Secure Crypto Engine */
#define SCE_BASE                0x40020000UL
#define SCE_CTRL_BASE           0x40028000UL
#define SCE_HASH_BASE           0x4002B000UL
#define SCE_AES_BASE            0x4002D000UL
#define SCE_TRNG_BASE           0x4002E000UL

/* Timers */
#define D11CTIME_BASE           0xE0000000UL
#define TICKTIMER_BASE          0xE001B000UL
#define TIMER0_BASE             0xE001C000UL
#define ATIMER_BASE             0x40043000UL
#define ATIMER_AO_BASE          0x40063000UL

/* ReRAM controller */
#define RRC_BASE                0x40000000UL

/* Default clocks (set by boot0/boot1) */
#define ACLK_HZ                 350000000UL
#define FCLK_HZ                 700000000UL

#endif
