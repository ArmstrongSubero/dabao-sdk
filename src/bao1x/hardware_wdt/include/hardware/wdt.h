/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Watchdog timer API for the Baochip-1x.
 * The WDT counts down from a load value. If it reaches zero
 * without being fed, it triggers an IRQ and/or system reset.
 */

#ifndef _HARDWARE_WDT_H
#define _HARDWARE_WDT_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the watchdog with a timeout in milliseconds (approximate). */
void wdt_start(uint32_t timeout_ms);

/* Start the watchdog with a raw countdown value (clock cycles). */
void wdt_start_raw(uint32_t load_value);

/* Feed (kick) the watchdog to prevent reset. */
void wdt_feed(void);

/* Read the current countdown value. */
uint32_t wdt_get_count(void);

#ifdef __cplusplus
}
#endif

#endif
