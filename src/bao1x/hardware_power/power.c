/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Power down driver for the Baochip-1x.
 */

#include "hardware/power.h"
#include "hardware/rtc.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pmu.h"
#include "hardware/regs/timer.h"
#include "sevs_runtime.h"

/*
 * Writing 0x5A to PDAR starts a state machine in ao_sysctrl.sv that holds
 * the SoC in reset and switches the PMU to the PMUCRPD regulator settings,
 * until a wake source that WKUPMASK leaves unmasked goes high. Then the
 * reset is released and the chip boots from boot0. Bits 17:8 of WKUPMASK
 * mask the sources, bit 8 + n for source n; a 1 masks it. PF1 (bit 16)
 * reads low on the Dabao and would cancel power down at once, so every
 * source not asked for is masked. Bits 7:0 enable the AO interrupt to
 * the CPU and stay 0.
 *
 * PMUCRPD 0x0C keeps POCENA and IOUTENA and turns off the internal 2.5 V
 * and 0.85 V regulators, as Xous deep_sleep does. On the Dabao the 0.85 V
 * buck still feeds the core, so power down holds it in reset rather than
 * cutting its supply. The 32 kHz clock select (AO CR) and the reset mask
 * are left as they are: the Dabao has no 32 kHz crystal, and selecting it
 * would stop the clock the state machine runs on.
 */
#define PMU_WAKE_ALL        0x3FFu
#define PMU_CRPD_OFF        0x0Cu
#define PMU_PDAR_KEY        0x5Au
#define POWER_DOWN_WAIT_MS  20u

/** @brief Enter power down until a wake source rises.
 *  @param[in] wake_sources POWER_WAKE_* bits.
 *  @return false if no source was given or power down did not happen.
 *  @req REQ-DABAO-PWR-0001 */
bool power_down(uint32_t wake_sources)
{
    wake_sources &= POWER_WAKE_RTC;
    SEVS_ASSERT((wake_sources & ~PMU_WAKE_ALL) == 0u);
    if (wake_sources == 0u) {
        return false;
    }

    if (wake_sources & POWER_WAKE_RTC) {
        rtc_alarm_wake(true);
    }

    PMU_WKUPMASK = (PMU_WAKE_ALL & ~wake_sources) << 8;
    PMU_CRPD = PMU_CRPD_OFF;
    memory_fence();
    PMU_PDAR = PMU_PDAR_KEY;
    memory_fence();

    uint32_t t0 = TICKTIMER_TIME0;
    while ((uint32_t)(TICKTIMER_TIME0 - t0) < POWER_DOWN_WAIT_MS) {
    }
    return false;
}
