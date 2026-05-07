/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART driver for the Baochip-1x UDMA UART subsystem.
 *
 * Transmit uses UDMA DMA transfers through an IFRAM buffer.
 * Receive uses the polling path (REG_VALID / REG_DATA).
 *
 * Code extracted from working main_sd_spi.c and main_uart.c,
 * both verified on Dabao hardware.
 */

#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/udma.h"
#include "sevs_runtime.h"

/* TX DMA buffer in IFRAM (must NOT be in SRAM) */
static uint8_t uart_tx_buf[256] __attribute__((section(".dma_buffers")));

/* Detected peripheral clock, set on first uart_init call */
static uint32_t s_perclk = 0;

static const uintptr_t uart_base[] = {
    UDMA_UART0_BASE,
    UDMA_UART1_BASE,
    UDMA_UART2_BASE,
    UDMA_UART3_BASE,
};

static const uint32_t uart_cg[] = {
    UDMA_CG_UART0,
    UDMA_CG_UART1,
    UDMA_CG_UART2,
    UDMA_CG_UART3,
};

static inline volatile uint32_t *uart_reg(uint inst, uint offset)
{
    return (volatile uint32_t *)(uart_base[inst] + offset);
}

/*
 * Detect perclk from boot1's UART2 configuration.
 * boot1 sets up UART2 at 1 Mbaud. The divider in SETUP[31:16]
 * equals perclk / 1_000_000, so perclk = divider * 1_000_000.
 */
static uint32_t detect_perclk(void)
{

    uint32_t setup = REG32(UDMA_UART2_BASE + UDMA_UART_SETUP_OFFSET);
    uint32_t div = setup >> 16;
    if (div > 0)
        return div * 1000000UL;
    return 175000000UL;  /* fallback */
}

/** @brief Initialize a UART instance with the given baud rate.
 *  @param[in] instance UART peripheral index (0-3).
 *  @param[in] baudrate Desired baud rate in Hz.
 *  @return Actual baud rate achieved after divider rounding.
 *  @req REQ-DABAO-UART-0001 */
uint32_t uart_init(uint instance, uint32_t baudrate)
{
    SEVS_ASSERT(instance <= 3);
    SEVS_ASSERT(baudrate > 0);

    if (instance > 3) return 0;

    /* Detect perclk on first call */
    if (s_perclk == 0)
        s_perclk = detect_perclk();

    /* Open UDMA clock gate for this UART */
    REG32(UDMA_CTRL_BASE) |= uart_cg[instance];

    /* Configure pins for UART2 (the common case on Dabao) */
    if (instance == 2) {
        /* PB13 = RX (AF1, input, pull-up) */
        gpio_set_function(GPIO_PORT_B, 13, GPIO_FUNC_AF1);
        gpio_set_dir(GPIO_PORT_B, 13, false);
        gpio_pull_up(GPIO_PORT_B, 13);
        /* PB14 = TX (AF1, output) */
        gpio_set_function(GPIO_PORT_B, 14, GPIO_FUNC_AF1);
        gpio_set_dir(GPIO_PORT_B, 14, true);
    }

    /* Compute divider: divider = (perclk + baud/2) / baud */
    uint32_t div = (s_perclk + baudrate / 2) / baudrate;

    /* Reset and configure: 8N1 with TX+RX enabled */
    *uart_reg(instance, UDMA_UART_SETUP_OFFSET) = 0;
    memory_fence();
    *uart_reg(instance, UDMA_UART_SETUP_OFFSET) = UART_SETUP_8N1 | (div << 16);
    memory_fence();

    return s_perclk / div;
}

/** @brief Transmit data via UART using DMA.
 *  @param[in] instance UART peripheral index (0-3).
 *  @param[out] data Buffer to transmit.
 *  @param[in] len Number of bytes to send.
 *  @req REQ-DABAO-UART-0002 */
void uart_write(uint instance, const uint8_t *data, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(data);
    volatile uint32_t *tx_cfg  = uart_reg(instance, UDMA_TX_CFG_OFFSET);
    volatile uint32_t *tx_addr = uart_reg(instance, UDMA_TX_SADDR_OFFSET);
    volatile uint32_t *tx_size = uart_reg(instance, UDMA_TX_SIZE_OFFSET);
    volatile uint32_t *status  = uart_reg(instance, UDMA_UART_STATUS_OFFSET);

    for (int s_guard = 0; s_guard < 100000 && (len > 0); s_guard++) {
        uint32_t chunk = (len > 256) ? 256 : len;

        for (uint32_t i = 0; i < chunk; i++)
            uart_tx_buf[i] = data[i];

        for (int s_poll = 0; s_poll < 1000000 && (*tx_cfg  & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
        for (int s_poll = 0; s_poll < 1000000 && (*status  & UART_STATUS_TX_BUSY); s_poll++) { /* bounded poll */ }

        *tx_addr = (uint32_t)(uintptr_t)uart_tx_buf;
        *tx_size = chunk;
        *tx_cfg  = UDMA_CFG_EN;
        memory_fence();

        for (int s_poll = 0; s_poll < 1000000 && (*tx_cfg  & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }
        for (int s_poll = 0; s_poll < 1000000 && (*status  & UART_STATUS_TX_BUSY); s_poll++) { /* bounded poll */ }

        data += chunk;
        len  -= chunk;
    }
}

/** @brief Transmit a single character via UART.
 *  @param[in] instance UART peripheral index (0-3).
 *  @param[in] c Character to send.
 *  @req REQ-DABAO-UART-0003 */
void uart_putc(uint instance, char c)
{
    SEVS_ASSERT(instance <= 3);

    uart_write(instance, (const uint8_t *)&c, 1);
}

/** @brief Send a null-terminated string via UART.
 *  @param[in] instance UART peripheral index (0-3).
 *  @param[in] s String to send.
 *  @req REQ-DABAO-UART-0006 */
void uart_puts(uint instance, const char *s)
{
    SEVS_REQUIRE_NOT_NULL(s);
    uint32_t len = 0;
    const char *p = s;
    for (int s_guard = 0; s_guard < 100000 && *p; s_guard++) { p++; len++; }
    uart_write(instance, (const uint8_t *)s, len);
}

/** @brief Check whether the UART receive register has data.
 *  @param[in] instance UART peripheral index (0-3).
 *  @return true if at least one byte is available.
 *  @req REQ-DABAO-UART-0004 */
bool uart_is_readable(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    return REG32(uart_base[instance] + UDMA_UART_VALID_OFFSET) & 1;
}

/** @brief Read a single character from UART, blocking until available.
 *  @param[in] instance UART peripheral index (0-3).
 *  @return Received character.
 *  @req REQ-DABAO-UART-0007 */
char uart_getc(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    for (int s_poll = 0; s_poll < 1000000 && !uart_is_readable(instance); s_poll++) { /* bounded poll */ }
    return (char)(REG32(uart_base[instance] + UDMA_UART_DATA_OFFSET) & 0xFF);
}

/** @brief Return the detected peripheral clock frequency.
 *  @return Peripheral clock in Hz.
 *  @req REQ-DABAO-UART-0005 */
uint32_t uart_get_perclk(void)
{

    if (s_perclk == 0)
        s_perclk = detect_perclk();
    return s_perclk;
}

static volatile bool uart_tx_busy[4] = { false, false, false, false };

/** @brief Start a non-blocking UART transmit via DMA.
 *  @param[in] instance UART peripheral index (0-3).
 *  @param[out] data Buffer to transmit.
 *  @param[in] len Number of bytes to send.
 *  @req REQ-DABAO-UART-0006 */
void uart_write_async(uint instance, const uint8_t *data, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(data);
    if (instance > 3 || len == 0) return;

    /* Wait for any previous async transfer to finish */
    for (int s_poll = 0; s_poll < 1000000 && (uart_tx_busy[instance]); s_poll++) { /* bounded poll */ }

    /* Cap at buffer size */
    if (len > 256) len = 256;

    /* Copy to IFRAM DMA buffer */
    for (uint32_t i = 0; i < len; i++)
        uart_tx_buf[i] = data[i];

    /* Wait for UDMA channel to be free */
    volatile uint32_t *tx_cfg  = uart_reg(instance, UDMA_TX_CFG_OFFSET);
    volatile uint32_t *tx_addr = uart_reg(instance, UDMA_TX_SADDR_OFFSET);
    volatile uint32_t *tx_size = uart_reg(instance, UDMA_TX_SIZE_OFFSET);

    for (int s_poll = 0; s_poll < 1000000 && (*tx_cfg  & UDMA_CFG_EN); s_poll++) { /* bounded poll */ }

    uart_tx_busy[instance] = true;

    /* Kick the UDMA transfer and return immediately */
    *tx_addr = (uint32_t)(uintptr_t)uart_tx_buf;
    *tx_size = len;
    *tx_cfg  = UDMA_CFG_EN;
    memory_fence();
}

/** @brief Check whether an async UART transmit has completed.
 *  @param[in] instance UART peripheral index (0-3).
 *  @return true if the transfer is finished.
 *  @req REQ-DABAO-UART-0007 */
bool uart_write_done(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    if (instance > 3) return true;

    volatile uint32_t *tx_cfg = uart_reg(instance, UDMA_TX_CFG_OFFSET);
    if (!(*tx_cfg & UDMA_CFG_EN)) {
        uart_tx_busy[instance] = false;
        return true;
    }
    return false;
}

/** @brief Block until an async UART transmit completes.
 *  @param[in] instance UART peripheral index (0-3).
 *  @req REQ-DABAO-UART-0008 */
void uart_write_wait(uint instance)
{
    SEVS_ASSERT(instance <= 3);

    for (int s_poll = 0; s_poll < 1000000 && !uart_write_done(instance); s_poll++) { /* bounded poll */ }
}
