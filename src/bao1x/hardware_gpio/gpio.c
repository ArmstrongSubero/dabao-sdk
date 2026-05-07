/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * GPIO driver for the Baochip-1x IOX.
 * Register addresses and sequences extracted from working bare metal code.
 */

#include "hardware/gpio.h"
#include "hardware/regs/iox.h"
#include "sevs_runtime.h"

/*
 * The CRAFSEL registers are arranged as 12 registers covering 6 ports,
 * 2 registers per port (low 8 pins, high 8 pins). Each pin gets 2 bits.
 *
 * The output, output-enable, pull-up, and input registers are 1 per port,
 * 1 bit per pin, in arrays of 6.
 */

/** @brief Number of GPIO ports on the Baochip-1x. */
#define GPIO_NUM_PORTS  6
/** @brief Maximum pin number per port. */
#define GPIO_MAX_PIN    15

/* Register access helpers. All offsets are relative to IOX_BASE. */

/** @brief Get CRAFSEL register for a given port and pin. */
static volatile uint32_t *s_crafsel_reg(uint port, uint pin)
{
    /* Register index: port * 2 + (pin >= 8 ? 1 : 0) */
    uint idx = port * 2 + (pin >> 3);
    return (volatile uint32_t *)(IOX_BASE + idx * 4);
}

/** @brief Get GPIO output register for a port. */
static volatile uint32_t *s_gpioout_reg(uint port)
{
    return (volatile uint32_t *)(IOX_BASE + IOX_GPIOOUT_PA_OFFSET + port * 4);
}

/** @brief Get GPIO output-enable register for a port. */
static volatile uint32_t *s_gpiooe_reg(uint port)
{
    return (volatile uint32_t *)(IOX_BASE + IOX_GPIOOE_PA_OFFSET + port * 4);
}

/** @brief Get GPIO pull-up register for a port. */
static volatile uint32_t *s_gpiopu_reg(uint port)
{
    return (volatile uint32_t *)(IOX_BASE + IOX_GPIOPU_PA_OFFSET + port * 4);
}

/** @brief Get GPIO input register for a port. */
static volatile uint32_t *s_gpioin_reg(uint port)
{
    return (volatile uint32_t *)(IOX_BASE + IOX_GPIOIN_PA_OFFSET + port * 4);
}

/** @brief Get Schmitt trigger register for a port. */
static volatile uint32_t *s_schmitt_reg(uint port)
{
    return (volatile uint32_t *)(IOX_BASE + IOX_SCHMITT_PA_OFFSET + port * 4);
}

/** @brief Set the alternate function for a pin.
 *  @param[in] port GPIO port (0-5).
 *  @param[in] pin  Pin number within port (0-15).
 *  @param[in] fn   Alternate function selection.
 *  @req REQ-DABAO-GPIO-0001 */
void gpio_set_function(uint port, uint pin, enum gpio_function fn)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    SEVS_ASSERT(pin <= GPIO_MAX_PIN);
    volatile uint32_t *reg = s_crafsel_reg(port, pin);
    uint shift = (pin & 7) * 2;
    uint32_t val = *reg;
    val &= ~(0x03 << shift);
    val |= ((fn & 0x03) << shift);
    *reg = val;
}

/** @brief Initialize a pin to GPIO mode, input direction, no pulls.
 *  @param[in] port GPIO port (0-5).
 *  @param[in] pin  Pin number within port (0-15).
 *  @req REQ-DABAO-GPIO-0002 */
void gpio_init(uint port, uint pin)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    SEVS_ASSERT(pin <= GPIO_MAX_PIN);
    gpio_set_function(port, pin, GPIO_FUNC_GPIO);
    gpio_set_dir(port, pin, false);
    gpio_disable_pulls(port, pin);
}

/** @brief Set pin direction.
 *  @param[in] port GPIO port (0-5).
 *  @param[in] pin  Pin number within port (0-15).
 *  @param[in] out  true for output, false for input.
 *  @req REQ-DABAO-GPIO-0003 */
void gpio_set_dir(uint port, uint pin, bool out)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    SEVS_ASSERT(pin <= GPIO_MAX_PIN);
    volatile uint32_t *reg = s_gpiooe_reg(port);
    if (out) {
        *reg |= (1 << pin);
    } else {
        *reg &= ~(1 << pin);
    }
}

/** @brief Drive a pin high or low.
 *  @param[in] port  GPIO port (0-5).
 *  @param[in] pin   Pin number within port (0-15).
 *  @param[in] value true for high, false for low.
 *  @req REQ-DABAO-GPIO-0004 */
void gpio_put(uint port, uint pin, bool value)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    SEVS_ASSERT(pin <= GPIO_MAX_PIN);
    volatile uint32_t *reg = s_gpioout_reg(port);
    if (value) {
        *reg |= (1 << pin);
    } else {
        *reg &= ~(1 << pin);
    }
}

/** @brief Read the current value of a pin.
 *  @param[in] port GPIO port (0-5).
 *  @param[in] pin  Pin number within port (0-15).
 *  @return true if pin is high, false if low.
 *  @req REQ-DABAO-GPIO-0005 */
bool gpio_get(uint port, uint pin)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    SEVS_ASSERT(pin <= GPIO_MAX_PIN);
    volatile uint32_t *reg = s_gpioin_reg(port);
    return (*reg >> pin) & 1;
}

/** @brief Toggle a pin.
 *  @param[in] port GPIO port (0-5).
 *  @param[in] pin  Pin number within port (0-15).
 *  @req REQ-DABAO-GPIO-0006 */
void gpio_toggle(uint port, uint pin)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    SEVS_ASSERT(pin <= GPIO_MAX_PIN);
    volatile uint32_t *reg = s_gpioout_reg(port);
    *reg ^= (1 << pin);
}

/** @brief Enable the internal pull-up resistor.
 *  @param[in] port GPIO port (0-5).
 *  @param[in] pin  Pin number within port (0-15).
 *  @req REQ-DABAO-GPIO-0007 */
void gpio_pull_up(uint port, uint pin)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    SEVS_ASSERT(pin <= GPIO_MAX_PIN);
    volatile uint32_t *reg = s_gpiopu_reg(port);
    *reg |= (1 << pin);
}

/** @brief Disable the pull-up resistor.
 *  @param[in] port GPIO port (0-5).
 *  @param[in] pin  Pin number within port (0-15).
 *  @req REQ-DABAO-GPIO-0008 */
void gpio_disable_pulls(uint port, uint pin)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    SEVS_ASSERT(pin <= GPIO_MAX_PIN);
    volatile uint32_t *reg = s_gpiopu_reg(port);
    *reg &= ~(1 << pin);
}

/** @brief Enable or disable the Schmitt trigger on a pin.
 *  @param[in] port   GPIO port (0-5).
 *  @param[in] pin    Pin number within port (0-15).
 *  @param[in] enable true to enable, false to disable.
 *  @req REQ-DABAO-GPIO-0009 */
void gpio_set_schmitt(uint port, uint pin, bool enable)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    SEVS_ASSERT(pin <= GPIO_MAX_PIN);
    volatile uint32_t *reg = s_schmitt_reg(port);
    if (enable) {
        *reg |= (1 << pin);
    } else {
        *reg &= ~(1 << pin);
    }
}

/** @brief Write all 16 pins of a port at once.
 *  @param[in] port  GPIO port (0-5).
 *  @param[in] value 16-bit value to write.
 *  @req REQ-DABAO-GPIO-0010 */
void gpio_put_all(uint port, uint32_t value)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    volatile uint32_t *reg = s_gpioout_reg(port);
    *reg = value;
}

/** @brief Read all 16 pins of a port at once.
 *  @param[in] port GPIO port (0-5).
 *  @return 16-bit value with pin states.
 *  @req REQ-DABAO-GPIO-0011 */
uint32_t gpio_get_all(uint port)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    volatile uint32_t *reg = s_gpioin_reg(port);
    return *reg;
}

/** @brief Set output direction for multiple pins using a bitmask.
 *  @param[in] port GPIO port (0-5).
 *  @param[in] mask Bitmask where 1 means output.
 *  @req REQ-DABAO-GPIO-0012 */
void gpio_set_dir_masked(uint port, uint32_t mask)
{
    SEVS_ASSERT(port < GPIO_NUM_PORTS);
    volatile uint32_t *reg = s_gpiooe_reg(port);
    *reg = mask;
}
