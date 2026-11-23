/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC (Real-Time Clock) API for the Baochip-1x.
 * ARM PL031 32-bit counter in the always-on domain, counting rising edges
 * of clk1hz. clk1hz is the internal RC oscillator clk32k divided by
 * 2 * (CLK1HZ_FD + 1) (ao_sysctrl.sv). The Dabao has no 32 kHz crystal and
 * clk32k measures about 24 kHz, so the count only runs at 1 Hz after the
 * divider is calibrated against the TickTimer.
 */

#ifndef _HARDWARE_RTC_H
#define _HARDWARE_RTC_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Stop the RTC. The divider boot1 set is left in place. */
void rtc_init(void);

/*
 * Set the RTC divider directly. 14-bit field; higher bits are masked off.
 * Tick rate is clk32k / (2 * (div + 1)). div = 0 measures about 0.7 of
 * that formula on the board, so do not calibrate from it.
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

/* Set the match value. The alarm flag sets when the counter reaches it. */
void rtc_set_alarm(uint32_t seconds);

/* True once the counter has reached the match value. */
bool rtc_alarm_fired(void);

/* Clear the alarm flag. */
void rtc_clear_alarm(void);

/* Let the alarm raise the RTC interrupt line, which is what wakes the
 * chip from power down. The setting lasts until power on or RESET. */
void rtc_alarm_wake(bool enable);

#ifdef __cplusplus
}
#endif

#endif
