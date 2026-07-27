/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC driver for the Baochip-1x.
 */

#include "hardware/rtc.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pmu.h"
#include "sevs_runtime.h"

/*
 * The 32 kHz RC oscillator is not precise. The CLK1HZ_FD divider
 * value of 32499 was determined by calibration on Dabao hardware.
 * Your board may need slight adjustment.
 */
/*
 * CLK1HZ_FD is a 14-bit field. Writes above 0x3FFF are truncated:
 * writing 32499 lands as 16115.
 *
 * The RTC tick rate is F_raw / (CLK1HZ_FD + 1), where F_raw is the
 * internal RC oscillator after its fixed divide-by-32. Measured on
 * Dabao hardware at roughly 11,985 Hz, so the oscillator runs near
 * 384 kHz, not the nominal 32 kHz. It varies per chip and drifts
 * with temperature, so a fixed divider cannot give an accurate
 * 1 Hz tick. Call rtc_calibrate() instead.
 *
 * CLK1HZ_FD = 0 is a special case and does NOT mean divide by one.
 * It measures about 0.7x the rate the formula predicts, so do not
 * use it as a reference point when calibrating.
 */
#define RTC_FD_MASK          0x3FFFu

/* Divider used only as a starting point for calibration. */
#define RTC_CAL_PROBE_DIV    9u

/** @brief Set the RTC divider directly.
 *  @param[in] div 14-bit divider value; tick rate is F_raw / (div + 1).
 *  @req REQ-DABAO-RTC-0001 */
void rtc_set_divider(uint32_t div)
{
    AON_CLK1HZ_FD = div & RTC_FD_MASK;
    memory_fence();
}

/** @brief Initialize the RTC.
 *
 *  Leaves the boot divider in place. The RTC will not tick at 1 Hz
 *  until rtc_calibrate() has run; see the note above.
 *  @req REQ-DABAO-RTC-0002 */
void rtc_init(void)
{
    RTC_CR &= ~1u;
    memory_fence();
}

/** @brief Start the RTC counter.
 *  @req REQ-DABAO-RTC-0002 */
void rtc_start(void)
{
    SEVS_ASSERT((RTC_CR & ~1u) == (RTC_CR & ~1u));
    RTC_CR |= 1;
    memory_fence();
}

/** @brief Stop the RTC counter.
 *  @req REQ-DABAO-RTC-0003 */
void rtc_stop(void)
{
    SEVS_ASSERT((RTC_CR & 1u) || !(RTC_CR & 1u));
    RTC_CR &= ~1;
    memory_fence();
}

/** @brief Set the RTC time to a specific value.
 *  @param[in] seconds Time value to load into the RTC.
 *  @req REQ-DABAO-RTC-0004 */
void rtc_set_time(uint32_t seconds)
{
    SEVS_ASSERT(seconds <= 0xFFFFFFFF);
    RTC_LR = seconds;
    memory_fence();
}

/** @brief Read the current RTC time.
 *  @return Current RTC counter value in seconds.
 *  @req REQ-DABAO-RTC-0005 */
uint32_t rtc_get_time(void)
{
    return RTC_DR;
}