/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * PWM driver for the Baochip-1x.
 */

#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pwm.h"
#include "sevs_runtime.h"

/* Per-slice state: period in ticks (counter top is period - 1) */
static uint16_t slice_period[4];

static inline volatile uint32_t *pwm_reg(uint slice, uint offset)
{
    return (volatile uint32_t *)(PWM_BASE + PWM_SLICE_OFFSET(slice) + offset);
}

static inline volatile uint32_t *pwm_prefd_reg(uint slice)
{
    return (volatile uint32_t *)(PWM_BASE + PWM_PREFD0_OFFSET + slice * 4);
}

/** @brief Configure a GPIO pin for PWM alternate function.
 *  @param[in] port GPIO port (PORT_B or PORT_C).
 *  @param[in] pin Pin number (0-3 for PWM channels).
 *  @req REQ-DABAO-PWM-0001 */
void pwm_init_pin(uint port, uint pin)
{

    /*
     * Both PWM1 (PB0-PB3) and PWM2 (PC0-PC3) use AF3.
     * Verified working on harware tests.
     */
    if ((port != GPIO_PORT_B && port != GPIO_PORT_C) || pin > 3)
        return;

    gpio_set_function(port, pin, GPIO_FUNC_AF3);
    gpio_set_dir(port, pin, true);
}

/** @brief Initialize a PWM slice at the given frequency and start it.
 *  @param[in] slice PWM slice index (0-3).
 *  @param[in] freq_hz Desired output frequency in Hz.
 *  @return Actual period in ticks (100 percent duty value).
 *  @req REQ-DABAO-PWM-0002 */
uint16_t pwm_init(uint slice, uint32_t freq_hz)
{
    SEVS_ASSERT(slice <= 3);
    SEVS_ASSERT(freq_hz > 0);

    /* Runtime guards: asserts may be compiled out in a release build. */
    if (slice > 3) return 0;
    if (freq_hz == 0) return 0;

    /*
     * CLKSEL=0 selects PCLK (43.75 MHz). The PREFD register is not
     * used in this mode. The prescaler in the CONFIG register (8-bit,
     * bits [23:16]) divides PCLK further.
     *
     * timer_clock = PCLK / (prescaler + 1)
     * frequency = timer_clock / period
     *
     */
    uint32_t pwm_clk = ACLK_HZ / 8;

    /* Enable this slice in the PWM clock gate */
    REG32(PWM_BASE + PWM_CG_OFFSET) |= (1 << slice);

    /* Stop and reset */
    *pwm_reg(slice, PWM_CMD_OFFSET) = PWM_CMD_STOP | PWM_CMD_RESET;
    memory_fence();

    /*
     * Find prescaler and period values.
     * Start with prescaler=0 (divide by 1). If period exceeds 16 bits,
     * increase the prescaler until it fits.
     */
    uint32_t prescaler;
    uint32_t period = 0;
    for (prescaler = 0; prescaler < 256; prescaler++) {
        period = (pwm_clk / (prescaler + 1)) / freq_hz;
        if (period <= 65535) break;
    }

    /*
     * If the loop ran to completion, prescaler is 256 on exit. The PRESC
     * field is 8 bits, so writing 256 truncates to 0 and selects the
     * fastest divider instead of the slowest. Clamp to the slowest
     * settings the hardware can actually produce.
     *
     * Slowest reachable frequency is PCLK / 256 / 65535, about 2.61 Hz.
     */
    if (prescaler > 255) {
        prescaler = 255;
        period = 65535;
    }

    /* The counter runs 0..top inclusive, so a period of N ticks needs
     * top = N - 1. Writing N as the top made every frequency one tick
     * slow (and 50% duty slightly under 50%). A period of 1 tick cannot
     * be expressed with a distinct set and clear point, so 2 is the
     * smallest period used. */
    if (period < 2) period = 2;

    slice_period[slice] = (uint16_t)period;

    /* PREFD not used with CLKSEL=0, leave at zero */
    *pwm_prefd_reg(slice) = 0;

    /*
     * CLKSEL = 0: use PCLK directly.
     * UPDOWNSEL = 1: count up and reset at threshold.
     * PRESC: divide PCLK by (prescaler + 1).
     */
    *pwm_reg(slice, PWM_CONFIG_OFFSET) = PWM_CFG_CLKSEL_REF
                                       | PWM_CFG_UPDOWN_UP
                                       | PWM_CFG_PRESC(prescaler);

    /* Counter range: 0 to period - 1, which is period ticks. */
    *pwm_reg(slice, PWM_THRESHOLD_OFFSET) = PWM_THRESHOLD_PACK(0, period - 1);

    /* All channels start at 0% duty: clear at counter 0, never set. */
    *pwm_reg(slice, PWM_TH_CH0_OFFSET) = PWM_TH_MODE_CLEAR | 0;
    *pwm_reg(slice, PWM_TH_CH1_OFFSET) = PWM_TH_MODE_CLEAR | 0;
    *pwm_reg(slice, PWM_TH_CH2_OFFSET) = PWM_TH_MODE_CLEAR | 0;
    *pwm_reg(slice, PWM_TH_CH3_OFFSET) = PWM_TH_MODE_CLEAR | 0;

    memory_fence();

    /* Arm and start */
    *pwm_reg(slice, PWM_CMD_OFFSET) = PWM_CMD_ARM;
    memory_fence();
    *pwm_reg(slice, PWM_CMD_OFFSET) = PWM_CMD_START;

    return (uint16_t)period;
}

/** @brief Set the duty cycle for a PWM channel as a raw counter value.
 *  @param[in] slice PWM slice index (0-3).
 *  @param[in] channel Channel within slice (0-3).
 *  @param[in] duty Counter compare value (0 to period).
 *  @req REQ-DABAO-PWM-0003 */
void pwm_set_duty(uint slice, uint channel, uint16_t duty)
{
    SEVS_ASSERT(slice <= 3);
    SEVS_ASSERT(channel <= 3);

    if (slice > 3 || channel > 3) return;

    if (duty > slice_period[slice])
        duty = slice_period[slice];

    /*
     * SET_CLR sets the output at counter 0 and clears it at the compare
     * match. The two ends of the range collide with that: a compare of
     * 0 puts the set and the clear on the same tick, and a compare at
     * the top used to coincide with the wrap. Both were seen to glitch
     * on hardware (the breathing example flashed at the ends). The ends
     * are therefore produced as constant levels: 0% clears at counter 0
     * and never sets, 100% sets at counter 0 and never clears.
     */
    uint32_t val;
    if (duty == 0) {
        val = PWM_TH_MODE_CLEAR | 0;
    } else if (duty >= slice_period[slice]) {
        val = PWM_TH_MODE_SET | 0;
    } else {
        val = PWM_TH_MODE_SET_CLR | (duty & 0xFFFF);
    }

    uint offset = PWM_TH_CH0_OFFSET + channel * 4;
    *pwm_reg(slice, offset) = val;
    *pwm_reg(slice, PWM_CMD_OFFSET) = PWM_CMD_UPDATE;
}

/** @brief Set the duty cycle for a PWM channel as a percentage.
 *  @param[in] slice PWM slice index (0-3).
 *  @param[in] channel Channel within slice (0-3).
 *  @param[in] percent Duty cycle 0-100.
 *  @req REQ-DABAO-PWM-0004 */
void pwm_set_percent(uint slice, uint channel, uint32_t percent)
{
    SEVS_ASSERT(slice <= 3);
    SEVS_ASSERT(channel <= 3);

    if (slice > 3 || channel > 3) return;

    if (percent > 100) percent = 100;
    uint16_t duty = (uint16_t)((uint32_t)slice_period[slice] * percent / 100);
    pwm_set_duty(slice, channel, duty);
}

/** @brief Stop a running PWM slice.
 *  @param[in] slice PWM slice index (0-3).
 *  @req REQ-DABAO-PWM-0005 */
void pwm_stop(uint slice)
{
    SEVS_ASSERT(slice <= 3);

    if (slice > 3) return;

    *pwm_reg(slice, PWM_CMD_OFFSET) = PWM_CMD_STOP;
}

/** @brief Return the period (counter top) of a PWM slice.
 *  @param[in] slice PWM slice index (0-3).
 *  @return Period value set during pwm_init.
 *  @req REQ-DABAO-PWM-0005 */
uint16_t pwm_get_period(uint slice)
{
    SEVS_ASSERT(slice <= 3);

    if (slice > 3) return 0;

    return slice_period[slice];
}
