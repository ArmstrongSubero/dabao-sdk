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
 * The RTC counts rising edges of clk1hz, which ao_sysctrl.sv makes by
 * toggling a register every CLK1HZ_FD + 1 cycles of clk32k, so the tick
 * rate is clk32k / (2 * (CLK1HZ_FD + 1)). The Dabao has no 32 kHz crystal,
 * and its internal RC oscillator measured about 23,970 Hz on the board
 * (749 ticks/s at FD 15, 1198 at FD 9, 5994 at FD 1). It varies per chip
 * and drifts with temperature, so a fixed divider cannot give an accurate
 * 1 Hz tick.
 *
 * CLK1HZ_FD is a 14-bit field. Writes above 0x3FFF are truncated:
 * writing 32499 lands as 16115.
 *
 * CLK1HZ_FD = 0 measures about 0.7x the rate the formula predicts, so do
 * not use it as a reference point when calibrating.
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


/** @brief Initialize the RTC by stopping it.
 *
 *  Leaves the boot divider in place, so the RTC will not tick at 1 Hz.
 *  An accurate tick requires measuring the oscillator on your own board
 *  and setting the divider with rtc_set_divider(). See the note at the
 *  top of this file.
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

/** @brief Set the match value for the alarm.
 *  @param[in] seconds Counter value at which the alarm flag sets.
 *  @req REQ-DABAO-RTC-0006 */
void rtc_set_alarm(uint32_t seconds)
{
    RTC_MR = seconds;
    RTC_ICR = 1;
    memory_fence();
}

/** @brief Check the alarm flag.
 *  @return True once the counter has reached the match value.
 *  @req REQ-DABAO-RTC-0006 */
bool rtc_alarm_fired(void)
{
    return (RTC_RIS & 1u) != 0;
}

/** @brief Clear the alarm flag.
 *  @req REQ-DABAO-RTC-0006 */
void rtc_clear_alarm(void)
{
    RTC_ICR = 1;
    memory_fence();
}

/** @brief Route the alarm to the RTC interrupt line.
 *  @param[in] enable true sets the interrupt mask bit, false clears it.
 *  @req REQ-DABAO-RTC-0007 */
void rtc_alarm_wake(bool enable)
{
    RTC_IMSC = enable ? 1u : 0u;
    memory_fence();
}

