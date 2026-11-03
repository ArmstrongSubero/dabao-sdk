/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Delay functions using the VexRiscv tick timer.
 */

#include "bao/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/timer.h"
#include "sevs_runtime.h"
static int s_ticktimer_initialized = 0;

static void ticktimer_init(void)
{
    if (s_ticktimer_initialized) return;
    TICKTIMER_CLOCKS_PER_TICK = ACLK_HZ / 1000;  /* 1 tick = 1 ms */
    memory_fence();
    s_ticktimer_initialized = 1;
}

/** @brief Millis.
 *  @return Milliseconds elapsed since boot.
 *  @req REQ-DABAO-DELAY-0001 */
uint64_t millis(void)
{
    // Explicit high, low, high to avoid race condition 
    if (!s_ticktimer_initialized) ticktimer_init();
    
    uint32_t hi_a;
    uint32_t lo;
    uint32_t hi_b;

    do {
        hi_a = TICKTIMER_TIME1;
        lo   = TICKTIMER_TIME0;
        hi_b = TICKTIMER_TIME1;
    } while (hi_a != hi_b);

    return ((uint64_t)hi_b << 32) | (uint64_t)lo;
}

/** @brief Delay ms.
 *  @param[in] ms Delay duration in milliseconds.
 *  @req REQ-DABAO-DELAY-0002 */
void delay_ms(uint32_t ms)
{
    if (!s_ticktimer_initialized) ticktimer_init();
    uint64_t start = millis();
    while ((millis() - start) < ms) {}
}

/* 64-bit read of the VexRiscv cycle counter, safe across the carry. */
static uint64_t read_cycles(void)
{
    uint32_t hi_a;
    uint32_t lo;
    uint32_t hi_b;

    do {
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi_a));
        __asm__ volatile ("csrr %0, mcycle"  : "=r"(lo));
        __asm__ volatile ("csrr %0, mcycleh" : "=r"(hi_b));
    } while (hi_a != hi_b);

    return ((uint64_t)hi_b << 32) | (uint64_t)lo;
}

/* Cycle counter rate, measured once against the tick timer. */
static uint32_t s_cycles_per_us = 0;

/* Fallback if the measurement looks wrong: the counter runs at the CPU
 * clock, which boot1 leaves at FCLK on the Dabao. */
#define CYCLES_PER_US_DEFAULT  (FCLK_HZ / 1000000UL)
#define CYCLES_PER_US_MIN      10UL
#define CYCLES_PER_US_MAX      2000UL
#define CALIBRATE_TICKS        4U

/* The old delay_us was a counted nop loop calibrated by guesswork for one
 * assumed clock and one assumed cycles per iteration, and it ran about
 * twice as long as asked. The cycle counter is exact but its rate depends
 * on the CPU clock, which the boot chain may change, so the rate is
 * measured against the millisecond tick timer (hardware verified) the
 * first time it is needed. Cost: about CALIBRATE_TICKS + 1 ms, once. */
uint32_t cycles_per_us(void)
{
    if (s_cycles_per_us != 0) {
        return s_cycles_per_us;
    }

    if (!s_ticktimer_initialized) ticktimer_init();

    uint64_t t0 = millis();
    /* Align to a tick edge so the window is a whole number of ticks. */
    while (millis() == t0) {}
    uint64_t c0 = read_cycles();
    uint64_t t1 = millis();
    while ((millis() - t1) < CALIBRATE_TICKS) {}
    uint64_t c1 = read_cycles();

    uint64_t per_us = (c1 - c0) / ((uint64_t)CALIBRATE_TICKS * 1000ULL);
    if (per_us < CYCLES_PER_US_MIN || per_us > CYCLES_PER_US_MAX) {
        per_us = CYCLES_PER_US_DEFAULT;
    }
    s_cycles_per_us = (uint32_t)per_us;
    return s_cycles_per_us;
}

/** @brief Delay us.
 *  @param[in] us Delay duration in microseconds.
 *  @req REQ-DABAO-DELAY-0003 */
void delay_us(uint32_t us)
{
    uint64_t target = (uint64_t)us * (uint64_t)cycles_per_us();
    uint64_t start  = read_cycles();
    while ((read_cycles() - start) < target) {}
}
