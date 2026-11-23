/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Power down for the Baochip-1x.
 */

#ifndef _HARDWARE_POWER_H
#define _HARDWARE_POWER_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wake sources for power_down(), the bits of the PMU wake mask. */
#define POWER_WAKE_RTC   (1u << 1)

/*
 * Hold the chip in power down until a wake source rises, after which it
 * boots again from the start. Returns false, without powering down, when
 * no wake source is given or one is already high.
 */
bool power_down(uint32_t wake_sources);

#ifdef __cplusplus
}
#endif

#endif
