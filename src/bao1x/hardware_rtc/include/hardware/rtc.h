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
