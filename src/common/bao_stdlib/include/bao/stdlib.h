/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Convenience functions: delays, printf, and system init.
 */

#ifndef _BAO_STDLIB_H
#define _BAO_STDLIB_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Millisecond delay using the hardware tick timer. */
void delay_ms(uint32_t ms);

/* Microsecond delay on the cycle counter. The counter rate is measured
 * against the tick timer on first use (about 5 ms, once). */
void delay_us(uint32_t us);

/* Measured cycle counter rate in cycles per microsecond. Measures on the
 * first call, which takes about 5 ms. Later calls return the cached value. */
uint32_t cycles_per_us(void);

/* Get milliseconds since boot (from tick timer). */
uint64_t millis(void);

/* Minimal printf routed to UART2. Supports %d, %u, %x, %s, %c, %% and zero
 * padded widths such as %04x. No floating point. */
void mini_printf(const char *fmt, ...);

/*
 * Initialize the system for user code. The startup code does not call it;
 * main() must (rovari_main.c does this before app_init()).
 *
 * Drains pending events, sets up UART2 at 115200 for printf and starts the
 * tick timer. It does not configure any LED pin.
 */
void bao_init(void);

/*
 * Drain every event array and leave every source disabled.
 *
 * Called first by bao_init. The only code that clears a pending event
 * is inside trap_dispatch, which runs only when an interrupt is taken,
 * which needs irq_init. A program that never calls irq_init has it
 * removed by --gc-sections and then nothing clears anything, ever.
 */
void bao_events_clear(void);

/* Set up UART2 for printf output. Called by bao_init(). */
void setup_default_uart(void);

#ifdef __cplusplus
}
#endif

#endif
