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

void delay_us(uint32_t us)
{
    /*
     * At 350 MHz, each nop loop iteration is roughly 3-4 cycles.
     * 350 cycles per microsecond / 4 cycles per iteration = ~87 iterations.
     * This is approximate but sufficient for short delays.
     */
    volatile uint32_t count;
    uint32_t iters = (us * 87) + 1;
    for (count = 0; count < iters; count++) {
        __asm__ volatile ("nop");
    }
}
