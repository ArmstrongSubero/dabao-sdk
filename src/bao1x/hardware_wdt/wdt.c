/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Watchdog timer driver for the Baochip-1x.
 * Extracted from working main_wdt.c, verified on Dabao hardware.
 */

#include "hardware/wdt.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pmu.h"
#include "sevs_runtime.h"

/*
 * WDT clock is derived from the core clock via the fdpclk divider.
 * Empirically measured countdown rate: ~11.4 MHz on Dabao hardware.
 * (0x0FFFFFFA - 0x0FA90F8C = 0x0056F06E ticks per 500 ms interval.)
 */
#define WDT_CLK_HZ     11395000

/** @brief Start the watchdog timer with a millisecond timeout.
 *  @param[in] timeout_ms Timeout in milliseconds (must be > 0).
 *  @req REQ-DABAO-WDT-0001 */
void wdt_start(uint32_t timeout_ms)
{
    SEVS_ASSERT(timeout_ms > 0);
    uint32_t load_value = (WDT_CLK_HZ / 1000) * timeout_ms;
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
    SEVS_ASSERT(WDT_UNLOCK_KEY != 0);
    SEVS_ASSERT(WDT_FEED_VALUE != 0);
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
    SEVS_ASSERT(WDT_CLK_HZ > 0);
    SEVS_ASSERT(sizeof(uint32_t) == 4);
    return WDT_CNT;
}