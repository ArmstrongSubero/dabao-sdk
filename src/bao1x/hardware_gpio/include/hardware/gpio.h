/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO API for the Baochip-1x IOX (I/O Crossbar).
 *
 * The Baochip-1x uses a port-based GPIO architecture with 6 ports (PA-PF),
 * 16 pins each. Unlike the RP2040's flat numbering, port and pin are always
 * specified as separate arguments.
 */

#ifndef _HARDWARE_GPIO_H
#define _HARDWARE_GPIO_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

enum gpio_port {
    GPIO_PORT_A = 0,
    GPIO_PORT_B = 1,
    GPIO_PORT_C = 2,
    GPIO_PORT_D = 3,
    GPIO_PORT_E = 4,
    GPIO_PORT_F = 5,
};

enum gpio_function {
    GPIO_FUNC_GPIO = 0x00,
    GPIO_FUNC_AF1  = 0x01,
    GPIO_FUNC_AF2  = 0x02,
    GPIO_FUNC_AF3  = 0x03,
};

enum gpio_drive_strength {
    GPIO_DRIVE_STRENGTH_2MA  = 0,
    GPIO_DRIVE_STRENGTH_4MA  = 1,
    GPIO_DRIVE_STRENGTH_8MA  = 2,
    GPIO_DRIVE_STRENGTH_12MA = 3,
};

/* Set a pin to GPIO mode, input direction, no pulls. */
void gpio_init(uint port, uint pin);

/* Set the alternate function for a pin. */
void gpio_set_function(uint port, uint pin, enum gpio_function fn);

/* Set pin direction. true = output, false = input. */
void gpio_set_dir(uint port, uint pin, bool out);

/* Drive a pin high or low. Pin must be set as output. */
void gpio_put(uint port, uint pin, bool value);

/* Read the current value of a pin. */
bool gpio_get(uint port, uint pin);

/* Toggle a pin. */
void gpio_toggle(uint port, uint pin);

/* Enable the internal pull-up resistor. Baochip-1x has pull-up only. */
void gpio_pull_up(uint port, uint pin);

/* Disable the pull-up resistor. */
void gpio_disable_pulls(uint port, uint pin);

/* Enable or disable the Schmitt trigger on an input pin. */
void gpio_set_schmitt(uint port, uint pin, bool enable);

/* Write all 16 pins of a port at once. */
void gpio_put_all(uint port, uint32_t value);

/* Read all 16 pins of a port at once. */
uint32_t gpio_get_all(uint port);

/* Set output direction for multiple pins using a bitmask. 1 = output. */
void gpio_set_dir_masked(uint port, uint32_t mask);

/* Return true if the pin is currently configured as output. */
bool gpio_get_dir(uint port, uint pin);

#ifdef __cplusplus
}
#endif

#endif
