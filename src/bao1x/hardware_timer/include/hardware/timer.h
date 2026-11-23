/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Timer and interrupt system for the Baochip-1x.
 *
 * The VexRiscv uses a custom interrupt controller with 20 "IRQ arrays".
 * Timer0 has its own dedicated interrupt line (IRQ 30).
 *
 * Timer0 is a simple down-counter clocked from ACLK (350 MHz).
 * It generates an interrupt when the count reaches zero and
 * automatically reloads.
 */

#ifndef _HARDWARE_TIMER_H
#define _HARDWARE_TIMER_H

#include "bao/platform.h"
#include "hardware/regs/timer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* VexRiscv custom interrupt CSRs. With BAO_MMU the inline helpers below
 * use the supervisor arrays SIM and SIP and sie, sstatus and scause
 * instead (see bao/platform.h); the names stay as they were. */
#define CSR_MIM             0xBC0
#define CSR_MIP_VEX         0xFC0

static inline uint32_t csr_read_mim(void) {
    uint32_t val;
    __asm__ volatile ("csrr %0, " BAO_CSR_XIM : "=r"(val));
    return val;
}

static inline void csr_write_mim(uint32_t val) {
    __asm__ volatile ("csrw " BAO_CSR_XIM ", %0" :: "r"(val));
}

static inline uint32_t csr_read_mip_vex(void) {
    uint32_t val;
    __asm__ volatile ("csrr %0, " BAO_CSR_XIP : "=r"(val));
    return val;
}

static inline uint32_t csr_read_mcause(void) {
    uint32_t val;
    __asm__ volatile ("csrr %0, " BAO_CSR_CAUSE : "=r"(val));
    return val;
}

/*
 * Start Timer0 with a periodic interrupt at the given interval.
 * The timer counts down from (ACLK/1000)*ms and auto-reloads.
 * Enables the Timer0 interrupt in MIM and sets mstatus.MIE.
 */
static inline void timer0_start_periodic_ms(uint32_t ms)
{
    uint32_t cycles = (ACLK_HZ / 1000) * ms;

    TIMER0_EN = 0;
    TIMER0_EV_ENABLE = 0;
    TIMER0_EV_PENDING = 1;
    memory_fence();

    TIMER0_LOAD = cycles;
    TIMER0_RELOAD = cycles;

    TIMER0_EV_ENABLE = 1;
    memory_fence();

    /* Enable Timer0 in VexRiscv interrupt mask */
    csr_write_mim(csr_read_mim() | (1 << TIMER0_IRQ));

    /* Enable machine external interrupts (mie.MEIE, bit 11) */
    __asm__ volatile ("csrs " BAO_CSR_IE ", %0" :: "r"(1 << BAO_EXT_IE_BIT));

    /* Enable global interrupts (mstatus.MIE, bit 3) */
    __asm__ volatile ("csrsi " BAO_CSR_STATUS ", " BAO_STATUS_IE_IMM);

    TIMER0_EN = 1;
}

/* Stop Timer0 and disable its interrupt. */
static inline void timer0_stop(void)
{
    TIMER0_EN = 0;
    TIMER0_EV_ENABLE = 0;
    TIMER0_EV_PENDING = 1;
    memory_fence();
}

/* Read the current countdown value. */
static inline uint32_t timer0_get_value(void)
{
    TIMER0_UPDATE_VALUE = 1;
    return TIMER0_VALUE;
}

#ifdef __cplusplus
}
#endif

#endif
