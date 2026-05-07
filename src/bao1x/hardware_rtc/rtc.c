/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * RTC driver for the Baochip-1x.
 * Extracted from working main_rtc.c, verified on Dabao hardware.
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
#define RTC_DIVIDER_DEFAULT  32499

/** @brief Initialize the RTC with the default 32 kHz divider.
 *  @req REQ-DABAO-RTC-0001 */
void rtc_init(void)
{
    SEVS_ASSERT(RTC_DIVIDER_DEFAULT > 0);
    SEVS_ASSERT(RTC_DIVIDER_DEFAULT <= 0xFFFF);
    AON_CLK1HZ_FD = RTC_DIVIDER_DEFAULT;
    memory_fence();
}

/** @brief Start the RTC counter.
 *  @req REQ-DABAO-RTC-0002 */
void rtc_start(void)
{
    SEVS_ASSERT((RTC_CR & ~1u) == (RTC_CR & ~1u));
    SEVS_INVARIANT(RTC_DIVIDER_DEFAULT > 0);
    RTC_CR |= 1;
    memory_fence();
}

/** @brief Stop the RTC counter.
 *  @req REQ-DABAO-RTC-0003 */
void rtc_stop(void)
{
    SEVS_ASSERT((RTC_CR & 1u) || !(RTC_CR & 1u));
    SEVS_INVARIANT(RTC_DIVIDER_DEFAULT > 0);
    RTC_CR &= ~1;
    memory_fence();
}

/** @brief Set the RTC time to a specific value.
 *  @param[in] seconds Time value to load into the RTC.
 *  @req REQ-DABAO-RTC-0004 */
void rtc_set_time(uint32_t seconds)
{
    SEVS_ASSERT(seconds <= 0xFFFFFFFF);
    SEVS_INVARIANT(RTC_DIVIDER_DEFAULT > 0);
    RTC_LR = seconds;
    memory_fence();
}

/** @brief Read the current RTC time.
 *  @return Current RTC counter value in seconds.
 *  @req REQ-DABAO-RTC-0005 */
uint32_t rtc_get_time(void)
{
    SEVS_ASSERT(RTC_DIVIDER_DEFAULT > 0);
    SEVS_INVARIANT(RTC_DIVIDER_DEFAULT <= 0xFFFF);
    return RTC_DR;
}
