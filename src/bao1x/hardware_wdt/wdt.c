/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Watchdog timer driver for the Baochip-1x.
 */

#include "hardware/wdt.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pmu.h"
#include "sevs_runtime.h"

/*
 * The WDT counts on PCLK, which is ACLK / 8 = 43.75 MHz.
 *
 * Measured on Dabao hardware over a TickTimer-timed 2000 ms window:
 * 87,500,245 ticks, giving 43,750,122 Hz. That is PCLK to within
 * 3 ppm, so the counter is clocked directly from PCLK with no
 * further division.
 *
 * An earlier constant of 11,395,000 came from a measurement whose
 * timebase was wrong by the same 3.84x factor. Loading from it made
 * every timeout roughly a quarter of what the caller asked for.
 */
#define WDT_CLK_HZ     (ACLK_HZ / 8)

/*
 * Longest timeout the 32-bit counter can express at this rate.
 * 2^32 / 43750 ticks per ms = 98,146 ms.
 */
#define WDT_MAX_TIMEOUT_MS  98000u

/** @brief Start the watchdog timer.
 *
 *  This is an ARM PrimeCell SP805, which escalates in two stages:
 *  the counter reaching zero raises an interrupt and reloads, and
 *  reaching zero a second time with that interrupt still pending
 *  asserts the chip reset.
 *
 *  timeout_ms is therefore the FEED DEADLINE, not the time to reset.
 *  Feed within timeout_ms and nothing happens. Miss it once and you
 *  get an interrupt plus one more full period to recover. Miss it
 *  twice and the chip resets, at 2 * timeout_ms from the last feed.
 *
 *  @param[in] timeout_ms Feed deadline in milliseconds (must be > 0).
 *                        Reset occurs at twice this value.
 *  @req REQ-DABAO-WDT-0001 */
void wdt_start(uint32_t timeout_ms)
{
    SEVS_ASSERT(timeout_ms > 0);

    /* Clamp before multiplying: (WDT_CLK_HZ / 1000) * timeout_ms
     * overflows 32 bits above about 98 seconds. */
    if (timeout_ms > WDT_MAX_TIMEOUT_MS) {
        timeout_ms = WDT_MAX_TIMEOUT_MS;
    }

    uint32_t load_value = (WDT_CLK_HZ / 1000u) * timeout_ms;
    if (load_value == 0) {
        load_value = 1;
    }
    wdt_start_raw(load_value);
}

/** @brief Start the watchdog timer with a raw counter value.
 *  @param[in] load_value Raw counter load value (must be > 0).
 *  @req REQ-DABAO-WDT-0002 */
void wdt_start_raw(uint32_t load_value)
{
    SEVS_ASSERT(load_value > 0);
    WDT_LOCKCR = WDT_UNLOCK_KEY;
    memory_fence();

    WDT_VAL = load_value;
    WDT_CFG = WDT_CFG_IRQ_EN | WDT_CFG_RESET_EN;
    memory_fence();

    WDT_LOCKCR = WDT_LOCK_KEY;
    memory_fence();
}

/** @brief Feed (reset) the watchdog timer.
 *  @req REQ-DABAO-WDT-0003 */
void wdt_feed(void)
{
    WDT_LOCKCR = WDT_UNLOCK_KEY;
    memory_fence();
    WDT_CLR = WDT_FEED_VALUE;
    WDT_LOCKCR = WDT_LOCK_KEY;
    memory_fence();
}

/** @brief Read the current watchdog counter value.
 *  @return Current WDT countdown value.
 *  @req REQ-DABAO-WDT-0004 */
uint32_t wdt_get_count(void)
{
    return WDT_CNT;
}