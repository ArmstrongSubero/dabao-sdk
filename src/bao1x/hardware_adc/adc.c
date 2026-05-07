/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * ADC driver for the Baochip-1x UDMA ADC.
 */

#include "hardware/adc.h"
#include "hardware/gpio.h"
#include "bao/stdlib.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/udma.h"
#include "sevs_runtime.h"

#define ADC0_RX_SADDR   REG32(UDMA_ADC_BASE + 0x00)
#define ADC0_RX_SIZE    REG32(UDMA_ADC_BASE + 0x04)
#define ADC0_RX_CFG     REG32(UDMA_ADC_BASE + 0x08)
#define ADC0_CR_CFG     REG32(UDMA_ADC_BASE + 0x10)

/* CR_CFG bit definitions */
#define ADC_BGCHOP_EN   (1 << 0)
#define ADC_TEMPBUF_EN  (1 << 1)
#define ADC_BGBUF_EN    (1 << 2)
#define ADC_EXTBUF_EN   (1 << 3)
#define ADC_DATACOUNT(n) (((n) & 0x1F) << 8)
#define ADC_TSEN        (1 << 13)
#define ADC_EN          (1 << 14)
#define ADC_RST         (1 << 15)
#define ADC_CLKFD(n)    (((n) & 0xFF) << 16)
#define ADC_SEL_TEMP    (0 << 24)
#define ADC_SEL_EXT     (1 << 24)
#define ADC_VINSEL(ch)  (((ch) & 0x3) << 26)

/* RX_CFG bits */
#define ADC_RX_CONTINUE (1 << 0)
#define ADC_RX_CONST    (2 << 1)
#define ADC_RX_START    (1 << 4)
#define ADC_RX_CLEAR    (1 << 5)

/** @brief Maximum number of ADC channels. */
#define ADC_MAX_CHANNEL 3

/** @brief Maximum DMA poll iterations before timeout. */
#define ADC_POLL_TIMEOUT 1000000

static uint32_t s_adc_rx_buf __attribute__((section(".dma_buffers")));

/** @brief Initialize the ADC subsystem and configure the analog input pin.
 *  @req REQ-DABAO-ADC-0001 */
void adc_init(void)
{
    SEVS_ASSERT(UDMA_ADC_BASE != 0);
    SEVS_ASSERT(ADC_MAX_VALUE > 0);
    /* Enable UDMA clock gate for ADC */
    REG32(UDMA_CTRL_BASE) |= UDMA_CG_ADC;
    memory_fence();

    /* PC9: analog input. Disable digital output and pull-up. */
    gpio_set_function(GPIO_PORT_C, 9, GPIO_FUNC_GPIO);
    gpio_set_dir(GPIO_PORT_C, 9, false);
    gpio_disable_pulls(GPIO_PORT_C, 9);
    memory_fence();
}

/** @brief Read a raw ADC value from the specified channel.
 *  @param[in] channel ADC channel number (0-3).
 *  @return Raw ADC reading masked to ADC_MAX_VALUE.
 *  @req REQ-DABAO-ADC-0002 */
uint32_t adc_read_raw(uint32_t channel)
{
    SEVS_ASSERT(channel <= ADC_MAX_CHANNEL);

    ADC0_CR_CFG = 0;
    ADC0_CR_CFG = ADC_RST;
    memory_fence();

    ADC0_CR_CFG |= ADC_TSEN;
    delay_us(10);
    ADC0_CR_CFG |= ADC_BGCHOP_EN | ADC_BGBUF_EN | ADC_EXTBUF_EN;
    delay_us(1);
    ADC0_CR_CFG |= ADC_EN;
    delay_us(90);

    ADC0_CR_CFG |= ADC_SEL_EXT | ADC_VINSEL(channel);
    ADC0_CR_CFG |= ADC_CLKFD(16) | ADC_DATACOUNT(16);
    memory_fence();

    ADC0_RX_CFG = ADC_RX_CLEAR;
    memory_fence();
    s_adc_rx_buf = 0;

    ADC0_RX_SADDR = (uint32_t)(uintptr_t)&s_adc_rx_buf;
    ADC0_RX_SIZE  = 4;
    ADC0_RX_CFG   = ADC_RX_CONST | ADC_RX_START;
    memory_fence();

    for (int s_poll = 0; s_poll < ADC_POLL_TIMEOUT && ADC0_RX_SIZE != 0; s_poll++) {
        /* bounded poll */
    }

    uint32_t raw = s_adc_rx_buf & ADC_MAX_VALUE;

    ADC0_CR_CFG = ADC_RST;
    memory_fence();
    ADC0_CR_CFG = 0;

    return raw;
}

/** @brief Read a raw temperature sensor value.
 *  @return Raw ADC reading from the internal temperature sensor.
 *  @req REQ-DABAO-ADC-0003 */
uint32_t adc_read_temp_raw(void)
{
    SEVS_ASSERT(ADC_MAX_VALUE > 0);
    SEVS_ASSERT(UDMA_ADC_BASE != 0);
    ADC0_CR_CFG = 0;
    ADC0_CR_CFG = ADC_RST;
    memory_fence();

    ADC0_CR_CFG |= ADC_TSEN;
    delay_us(10);
    ADC0_CR_CFG |= ADC_BGCHOP_EN | ADC_BGBUF_EN | ADC_TEMPBUF_EN;
    delay_us(1);
    ADC0_CR_CFG |= ADC_EN;
    delay_us(90);

    ADC0_CR_CFG |= ADC_CLKFD(16) | ADC_DATACOUNT(16);
    memory_fence();

    ADC0_RX_CFG = ADC_RX_CLEAR;
    memory_fence();
    s_adc_rx_buf = 0;

    ADC0_RX_SADDR = (uint32_t)(uintptr_t)&s_adc_rx_buf;
    ADC0_RX_SIZE  = 4;
    ADC0_RX_CFG   = ADC_RX_CONST | ADC_RX_START;
    memory_fence();

    for (int s_poll = 0; s_poll < ADC_POLL_TIMEOUT && ADC0_RX_SIZE != 0; s_poll++) {
        /* bounded poll */
    }

    uint32_t raw = s_adc_rx_buf & ADC_MAX_VALUE;

    ADC0_CR_CFG = ADC_RST;
    memory_fence();
    ADC0_CR_CFG = 0;

    return raw;
}

/** @brief Convert a raw ADC reading to millivolts.
 *  @param[in] raw Raw ADC value.
 *  @return Voltage in millivolts.
 *  @req REQ-DABAO-ADC-0004 */
uint32_t adc_raw_to_mv(uint32_t raw)
{
    SEVS_ASSERT(ADC_VREF_MV > 0);
    SEVS_ASSERT(ADC_MAX_VALUE > 0);
    if (raw == 0) {
        return 0;
    }
    return (ADC_VREF_MV * ADC_MAX_VALUE) / raw;
}
