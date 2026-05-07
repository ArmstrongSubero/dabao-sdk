/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * PWM API for the Baochip-1x.
 *
 * The chip has 4 PWM slices (0-3), each with 4 channels (0-3).
 * On the Dabao board:
 *   Slice 1: PB0(ch0), PB1(ch1), PB2(ch2), PB3(ch3) via AF3
 *   Slice 2: PC0(ch0), PC1(ch1), PC2(ch2), PC3(ch3) via AF1
 */

#ifndef _HARDWARE_PWM_H
#define _HARDWARE_PWM_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize a PWM slice at the given frequency.
 * Configures the clock divider and counter period automatically.
 * Returns the actual period (counter top value) for duty cycle calculations.
 */
uint16_t pwm_init(uint slice, uint32_t freq_hz);

/* Set the raw duty count for a channel (0 to period). */
void pwm_set_duty(uint slice, uint channel, uint16_t duty);

/* Set duty as a percentage (0 to 100). */
void pwm_set_percent(uint slice, uint channel, uint32_t percent);

/* Stop a PWM slice. */
void pwm_stop(uint slice);

/* Get the period (counter top) for a running slice. */
uint16_t pwm_get_period(uint slice);

/*
 * Convenience: set up a GPIO pin for PWM output.
 * Configures the alternate function and output enable.
 * For PB1: pwm_init_pin(GPIO_PORT_B, 1) sets AF3.
 * For PC0: pwm_init_pin(GPIO_PORT_C, 0) sets AF1.
 */
void pwm_init_pin(uint port, uint pin);

#ifdef __cplusplus
}
#endif

#endif
