/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Timer register definitions for the Baochip-1x.
 * Covers Timer0, TickTimer, ATimer (AT0/AT1), and D11CTIME.
 */

#ifndef _HARDWARE_REGS_TIMER_H
#define _HARDWARE_REGS_TIMER_H

#include "hardware/regs/addressmap.h"

/* Timer0 - countdown timer with auto-reload and IRQ 30 */
#define TIMER0_LOAD         REG32(TIMER0_BASE + 0x00)
#define TIMER0_RELOAD       REG32(TIMER0_BASE + 0x04)
#define TIMER0_EN           REG32(TIMER0_BASE + 0x08)
#define TIMER0_UPDATE_VALUE REG32(TIMER0_BASE + 0x0C)
#define TIMER0_VALUE        REG32(TIMER0_BASE + 0x10)
#define TIMER0_EV_STATUS    REG32(TIMER0_BASE + 0x14)
#define TIMER0_EV_PENDING   REG32(TIMER0_BASE + 0x18)
#define TIMER0_EV_ENABLE    REG32(TIMER0_BASE + 0x1C)
#define TIMER0_IRQ          30

/* TickTimer - 64-bit free-running millisecond counter */
#define TICKTIMER_CONTROL        REG32(TICKTIMER_BASE + 0x00)
#define TICKTIMER_TIME1          REG32(TICKTIMER_BASE + 0x04)
#define TICKTIMER_TIME0          REG32(TICKTIMER_BASE + 0x08)
#define TICKTIMER_MSLEEP_TARGET1 REG32(TICKTIMER_BASE + 0x0C)
#define TICKTIMER_MSLEEP_TARGET0 REG32(TICKTIMER_BASE + 0x10)
#define TICKTIMER_EV_STATUS      REG32(TICKTIMER_BASE + 0x14)
#define TICKTIMER_EV_PENDING     REG32(TICKTIMER_BASE + 0x18)
#define TICKTIMER_EV_ENABLE      REG32(TICKTIMER_BASE + 0x1C)
#define TICKTIMER_CLOCKS_PER_TICK REG32(TICKTIMER_BASE + 0x20)
#define TICKTIMER_IRQ            20

/* ATimer - two-channel up-counter with compare match */
#define AT0_CFG             REG32(ATIMER_BASE + 0x00)
#define AT0_CNT             REG32(ATIMER_BASE + 0x08)
#define AT0_CMP             REG32(ATIMER_BASE + 0x10)
#define AT0_START           REG32(ATIMER_BASE + 0x18)
#define AT0_RESET           REG32(ATIMER_BASE + 0x20)

#define AT1_CFG             REG32(ATIMER_BASE + 0x04)
#define AT1_CNT             REG32(ATIMER_BASE + 0x0C)
#define AT1_CMP             REG32(ATIMER_BASE + 0x14)
#define AT1_START           REG32(ATIMER_BASE + 0x1C)
#define AT1_RESET           REG32(ATIMER_BASE + 0x24)

/* ATimer config bits */
#define AT_CFG_EN           (1 << 0)
#define AT_CFG_RST          (1 << 1)
#define AT_CFG_IRQEN        (1 << 2)
#define AT_CFG_MODE_INC     (0 << 4)    /* keep counting past compare */
#define AT_CFG_MODE_RST     (1 << 4)    /* reset counter on compare match */
#define AT_CFG_REPEAT       (0 << 5)
#define AT_CFG_ONESHOT      (1 << 5)
#define AT_CFG_PEN          (1 << 6)    /* prescaler enable */
#define AT_CFG_CLK_PCLK     (0 << 7)   /* ~44 MHz */
#define AT_CFG_CLK_32K      (1 << 7)   /* ~48 kHz */
#define AT_CFG_PVAL(n)      (((n) & 0xFF) << 8)
#define AT_CFG_CASCADE      (1 << 31)   /* AT1 clocked by AT0 overflow */

#define AT_START_EN         (1 << 0)
#define AT_RESET_RST        (1 << 0)

/* D11CTIME - simple heartbeat toggle, no IRQ */
#define D11CTIME_CONTROL    REG32(D11CTIME_BASE + 0x00)
#define D11CTIME_HEARTBEAT  REG32(D11CTIME_BASE + 0x04)

#endif
