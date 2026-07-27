/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC (Real-Time Clock) API for the Baochip-1x.
 * 32-bit counter running at 1 Hz (after divider calibration).
 * Survives sleep modes via the always-on power domain.
 */

#ifndef _HARDWARE_RTC_H
#define _HARDWARE_RTC_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize the RTC with the 1 Hz divider. */
void rtc_init(void);

/*
 * Set the RTC divider directly. 14-bit field; higher bits are masked off.
 * Tick rate is F_raw / (div + 1), where F_raw is the internal RC
 * oscillator after its fixed /32. Do not use div = 0: it is a special
 * case that does not follow the formula.
 */
void rtc_set_divider(uint32_t div);

/* Start the RTC counter. */
void rtc_start(void);

/* Stop the RTC counter. */
void rtc_stop(void);

/* Set the RTC counter value (seconds since epoch). */
void rtc_set_time(uint32_t seconds);

/* Read the current RTC counter value. */
uint32_t rtc_get_time(void);

#ifdef __cplusplus
}
#endif

#endif