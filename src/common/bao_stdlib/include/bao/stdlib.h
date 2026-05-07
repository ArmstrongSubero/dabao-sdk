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

/* Microsecond delay using busy-wait. Approximate. */
void delay_us(uint32_t us);

/* Get milliseconds since boot (from tick timer). */
uint64_t millis(void);

/* Minimal printf routed to UART2. Supports %d, %u, %x, %s, %c, %%. */
void mini_printf(const char *fmt, ...);

/*
 * Initialize the system for user code.
 * Called automatically by the C runtime before main().
 *
 * Sets up: tick timer, UART2 at 115200 for printf, LED pin (PB1) as GPIO output.
 */
void bao_init(void);

/* Set up UART2 for printf output. Called by bao_init(). */
void setup_default_uart(void);

#ifdef __cplusplus
}
#endif

#endif
