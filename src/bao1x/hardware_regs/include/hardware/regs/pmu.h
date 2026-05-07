/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Clock Generation Unit (CGU) and Power Management Unit (PMU) registers.
 */

#ifndef _HARDWARE_REGS_PMU_H
#define _HARDWARE_REGS_PMU_H

#include "hardware/regs/addressmap.h"

/* CGU registers */
#define CGU_LP              REG32(CGU_BASE + 0x04)
#define CGU_SEL0            REG32(CGU_BASE + 0x10)
#define CGU_FD_FCLK         REG32(CGU_BASE + 0x14)
#define CGU_FD_ACLK         REG32(CGU_BASE + 0x18)
#define CGU_SET             REG32(CGU_BASE + 0x2C)
#define CGU_SEL1            REG32(CGU_BASE + 0x30)

/* PMU registers (in the AON domain) */
#define PMU_CR              REG32(AON_BASE + 0x10)
#define PMU_SR              REG32(AON_BASE + 0x38)

/* RTC registers */
#define RTC_DR              REG32(RTC_BASE + 0x00)
#define RTC_MR              REG32(RTC_BASE + 0x04)
#define RTC_LR              REG32(RTC_BASE + 0x08)
#define RTC_CR              REG32(RTC_BASE + 0x0C)
#define RTC_IMSC            REG32(RTC_BASE + 0x10)
#define RTC_ICR             REG32(RTC_BASE + 0x1C)

/* AON clock registers */
#define AON_CLK32K_SEL      REG32(AON_BASE + 0x00)
#define AON_CLK1HZ_FD       REG32(AON_BASE + 0x04)

/* WDT registers */
#define WDT_VAL             REG32(WDT_BASE + 0x00)
#define WDT_CNT             REG32(WDT_BASE + 0x04)
#define WDT_CFG             REG32(WDT_BASE + 0x08)
#define WDT_CLR             REG32(WDT_BASE + 0x0C)
#define WDT_INTRAW          REG32(WDT_BASE + 0x10)
#define WDT_INT             REG32(WDT_BASE + 0x14)
#define WDT_LOCKCR          REG32(0x40041C00UL)

/* WDT constants */
#define WDT_UNLOCK_KEY      0x1ACCE551UL
#define WDT_LOCK_KEY        0x00000000UL
#define WDT_FEED_VALUE      0x5AUL
#define WDT_CFG_IRQ_EN      (1 << 0)
#define WDT_CFG_RESET_EN    (1 << 1)

/* ReRAM controller */
#define RRC_CR              REG32(RRC_BASE + 0x00)

#endif
