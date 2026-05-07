/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC API for the Baochip-1x.
 *
 * 10-bit ADC with internal bandgap reference (1208 mV) and
 * on-chip temperature sensor. External inputs on PC9.
 */

#ifndef _HARDWARE_ADC_H
#define _HARDWARE_ADC_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_RESOLUTION      10
#define ADC_MAX_VALUE       ((1 << ADC_RESOLUTION) - 1)
#define ADC_VREF_MV         1208

/* Initialize the ADC subsystem. Configures pin PC9 as analog input. */
void adc_init(void);

/* Read a raw 10-bit value from the specified channel (0-3). */
uint32_t adc_read_raw(uint32_t channel);

/* Read the internal temperature sensor (raw 10-bit value). */
uint32_t adc_read_temp_raw(void);

/* Convert a raw ADC value to millivolts (using the 1208 mV bandgap). */
uint32_t adc_raw_to_mv(uint32_t raw);

#ifdef __cplusplus
}
#endif

#endif
