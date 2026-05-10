/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART driver for the Baochip-1x UDMA UART subsystem.
 *
 * Transmit uses UDMA DMA transfers through an IFRAM buffer.
 * Receive uses the polling path (REG_VALID / REG_DATA).
 */

#include "hardware/uart.h"
#include "hardware/gpio.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/udma.h"
#include "sevs_runtime.h"

/* Local constants */
#define UART_INSTANCE_COUNT        4u
#define UART_TX_BUF_SIZE           256u
#define UART_POLL_LIMIT            1000000u
#define UART_WRITE_CHUNK_LIMIT     100000u
#define UART_PUTS_MAX_LEN          100000u

#ifndef UART_OK
#define UART_OK                    0
#define UART_ERROR_INVALID        -1
#define UART_ERROR_NULL           -2
#define UART_ERROR_SIZE           -3
#define UART_ERROR_BUSY           -4
#define UART_ERROR_TIMEOUT        -5
#endif

/* TX DMA buffer in IFRAM. The UDMA engine cannot read from normal SRAM. */
static uint8_t uart_tx_buf[UART_TX_BUF_SIZE]
    __attribute__((section(".dma_buffers")));

/* Detected peripheral clock, set on first uart_init() call. */
static uint32_t s_perclk = 0u;

/* Async TX state. */
static volatile bool s_uart_tx_busy[UART_INSTANCE_COUNT] = {
    false, false, false, false
};

static volatile bool s_uart_tx_notified[UART_INSTANCE_COUNT] = {
    false, false, false, false
};

static const uintptr_t uart_base[UART_INSTANCE_COUNT] = {
    UDMA_UART0_BASE,
    UDMA_UART1_BASE,
    UDMA_UART2_BASE,
    UDMA_UART3_BASE,
};

static const uint32_t uart_cg[UART_INSTANCE_COUNT] = {
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
 *
 * boot1 sets up UART2 at 1 Mbaud. The divider in SETUP[31:16]
 * equals perclk / 1_000_000, so perclk = divider * 1_000_000.
 */
static uint32_t detect_perclk(void)
{
    uint32_t setup;
    uint32_t div;

    setup = REG32(UDMA_UART2_BASE + UDMA_UART_SETUP_OFFSET);
    div = setup >> 16;

    if (div > 0u) {
        return div * 1000000UL;
    }

    return 175000000UL;
}

/**
 * @brief Default UART transmit-complete hook.
 *
 * Applications may override this weak function by defining their own
 * non-weak uart_tx_complete_hook().
 *
 * This avoids function pointers while still giving callback-like behavior.
 *
 * @param[in] instance UART peripheral index.
 * @param[in] status Completion status.
 *
 * @req REQ-DABAO-UART-0009
 */
__attribute__((weak)) void uart_tx_complete_hook(uint instance, int status)
{
    (void)instance;
    (void)status;
}

/**
 * @brief Initialize a UART instance with the given baud rate.
 *
 * @param[in] instance UART peripheral index. Valid range: 0-3.
 * @param[in] baudrate Desired baud rate in Hz. Must be greater than zero.
 *
 * @return Actual baud rate achieved after divider rounding.
 * @return 0 if the instance or baudrate is invalid.
 *
 * @req REQ-DABAO-UART-0001
 */
uint32_t uart_init(uint instance, uint32_t baudrate)
{
    uint32_t div;

    SEVS_ASSERT(instance < UART_INSTANCE_COUNT);
    SEVS_ASSERT(baudrate > 0u);

    if (instance >= UART_INSTANCE_COUNT) {
        return 0u;
    }

    if (baudrate == 0u) {
        return 0u;
    }

    if (s_perclk == 0u) {
        s_perclk = detect_perclk();
    }

    REG32(UDMA_CTRL_BASE) |= uart_cg[instance];

    if (instance == 2u) {
        /* PB13 = RX, AF1, input, pull-up. */
        gpio_set_function(GPIO_PORT_B, 13u, GPIO_FUNC_AF1);
        gpio_set_dir(GPIO_PORT_B, 13u, false);
        gpio_pull_up(GPIO_PORT_B, 13u);

        /* PB14 = TX, AF1, output. */
        gpio_set_function(GPIO_PORT_B, 14u, GPIO_FUNC_AF1);
        gpio_set_dir(GPIO_PORT_B, 14u, true);
    }

    div = (s_perclk + (baudrate / 2u)) / baudrate;

    if (div == 0u) {
        div = 1u;
    }

    *uart_reg(instance, UDMA_UART_SETUP_OFFSET) = 0u;
    memory_fence();

    *uart_reg(instance, UDMA_UART_SETUP_OFFSET) =
        UART_SETUP_8N1 | (div << 16);

    memory_fence();

    return s_perclk / div;
}

/**
 * @brief Transmit data via UART using UDMA.
 *
 * This is the blocking transmit path. It sends data in bounded 256-byte
 * chunks through the IFRAM staging buffer.
 *
 * @param[in] instance UART peripheral index. Valid range: 0-3.
 * @param[in] data Buffer to transmit. Must not be NULL.
 * @param[in] len Number of bytes to send.
 *
 * @req REQ-DABAO-UART-0002
 */
void uart_write(uint instance, const uint8_t *data, uint32_t len)
{
    volatile uint32_t *tx_cfg;
    volatile uint32_t *tx_addr;
    volatile uint32_t *tx_size;
    volatile uint32_t *status;
    uint32_t chunk;
    uint32_t i;
    uint32_t guard;
    uint32_t poll;

    SEVS_ASSERT(instance < UART_INSTANCE_COUNT);
    SEVS_REQUIRE_NOT_NULL(data);

    if (instance >= UART_INSTANCE_COUNT) {
        return;
    }

    if (data == NULL) {
        return;
    }

    tx_cfg = uart_reg(instance, UDMA_TX_CFG_OFFSET);
    tx_addr = uart_reg(instance, UDMA_TX_SADDR_OFFSET);
    tx_size = uart_reg(instance, UDMA_TX_SIZE_OFFSET);
    status = uart_reg(instance, UDMA_UART_STATUS_OFFSET);

    for (guard = 0u;
         ((guard < UART_WRITE_CHUNK_LIMIT) && (len > 0u));
         guard++) {

        chunk = len;

        if (chunk > UART_TX_BUF_SIZE) {
            chunk = UART_TX_BUF_SIZE;
        }

        for (poll = 0u;
             ((poll < UART_POLL_LIMIT) && ((*tx_cfg & UDMA_CFG_EN) != 0u));
             poll++) {
            /* bounded poll */
        }

        for (poll = 0u;
             ((poll < UART_POLL_LIMIT) &&
              ((*status & UART_STATUS_TX_BUSY) != 0u));
             poll++) {
            /* bounded poll */
        }

        if (((*tx_cfg & UDMA_CFG_EN) != 0u) ||
            ((*status & UART_STATUS_TX_BUSY) != 0u)) {
            return;
        }

        for (i = 0u; i < chunk; i++) {
            uart_tx_buf[i] = data[i];
        }

        *tx_addr = (uint32_t)(uintptr_t)uart_tx_buf;
        *tx_size = chunk;
        *tx_cfg = UDMA_CFG_EN | UDMA_CFG_SIZE_8;
        memory_fence();

        for (poll = 0u;
             ((poll < UART_POLL_LIMIT) && ((*tx_cfg & UDMA_CFG_EN) != 0u));
             poll++) {
            /* bounded poll */
        }

        for (poll = 0u;
             ((poll < UART_POLL_LIMIT) &&
              ((*status & UART_STATUS_TX_BUSY) != 0u));
             poll++) {
            /* bounded poll */
        }

        if (((*tx_cfg & UDMA_CFG_EN) != 0u) ||
            ((*status & UART_STATUS_TX_BUSY) != 0u)) {
            return;
        }

        data += chunk;
        len -= chunk;
    }
}

/**
 * @brief Transmit a single character via UART.
 *
 * @param[in] instance UART peripheral index. Valid range: 0-3.
 * @param[in] c Character to send.
 *
 * @req REQ-DABAO-UART-0003
 */
void uart_putc(uint instance, char c)
{
    SEVS_ASSERT(instance < UART_INSTANCE_COUNT);

    if (instance >= UART_INSTANCE_COUNT) {
        return;
    }

    uart_write(instance, (const uint8_t *)&c, 1u);
}

/**
 * @brief Send a null-terminated string via UART.
 *
 * The string scan is bounded to avoid unbounded loops.
 *
 * @param[in] instance UART peripheral index. Valid range: 0-3.
 * @param[in] s String to send. Must not be NULL.
 *
 * @req REQ-DABAO-UART-0006
 */
void uart_puts(uint instance, const char *s)
{
    uint32_t len;
    uint32_t guard;

    SEVS_ASSERT(instance < UART_INSTANCE_COUNT);
    SEVS_REQUIRE_NOT_NULL(s);

    if (instance >= UART_INSTANCE_COUNT) {
        return;
    }

    if (s == NULL) {
        return;
    }

    len = 0u;

    for (guard = 0u;
         ((guard < UART_PUTS_MAX_LEN) && (s[len] != '\0'));
         guard++) {
        len++;
    }

    uart_write(instance, (const uint8_t *)s, len);
}

/**
 * @brief Check whether the UART receive register has data.
 *
 * @param[in] instance UART peripheral index. Valid range: 0-3.
 *
 * @return true if at least one byte is available.
 * @return false if no byte is available or instance is invalid.
 *
 * @req REQ-DABAO-UART-0004
 */
bool uart_is_readable(uint instance)
{
    SEVS_ASSERT(instance < UART_INSTANCE_COUNT);

    if (instance >= UART_INSTANCE_COUNT) {
        return false;
    }

    return ((REG32(uart_base[instance] + UDMA_UART_VALID_OFFSET) & 1u) != 0u);
}

/**
 * @brief Read a single character from UART.
 *
 * This function uses a bounded poll. If no data arrives before the timeout,
 * zero is returned.
 *
 * @param[in] instance UART peripheral index. Valid range: 0-3.
 *
 * @return Received character.
 * @return 0 if instance is invalid or no character arrives before timeout.
 *
 * @req REQ-DABAO-UART-0007
 */
char uart_getc(uint instance)
{
    uint32_t poll;

    SEVS_ASSERT(instance < UART_INSTANCE_COUNT);

    if (instance >= UART_INSTANCE_COUNT) {
        return (char)0;
    }

    for (poll = 0u;
         ((poll < UART_POLL_LIMIT) && (uart_is_readable(instance) == false));
         poll++) {
        /* bounded poll */
    }

    if (uart_is_readable(instance) == false) {
        return (char)0;
    }

    return (char)(REG32(uart_base[instance] + UDMA_UART_DATA_OFFSET) & 0xFFu);
}

/**
 * @brief Return the detected peripheral clock frequency.
 *
 * @return Peripheral clock in Hz.
 *
 * @req REQ-DABAO-UART-0005
 */
uint32_t uart_get_perclk(void)
{
    if (s_perclk == 0u) {
        s_perclk = detect_perclk();
    }

    return s_perclk;
}

/**
 * @brief Start a non-blocking UART transmit via UDMA.
 *
 * The data is copied into an internal IFRAM staging buffer before this function
 * returns. Therefore, the caller's data buffer does not need to remain valid
 * after this call.
 *
 * This simple async API supports one transfer of up to 256 bytes at a time.
 *
 * @param[in] instance UART peripheral index. Valid range: 0-3.
 * @param[in] data Buffer to transmit. Must not be NULL.
 * @param[in] len Number of bytes to send. Valid range: 1-256.
 *
 * @return UART_OK if the transfer started.
 * @return UART_ERROR_INVALID if instance is invalid.
 * @return UART_ERROR_NULL if data is NULL.
 * @return UART_ERROR_SIZE if len is zero or greater than 256.
 * @return UART_ERROR_BUSY if a transfer is already active.
 *
 * @req REQ-DABAO-UART-0006
 */
int uart_write_async(uint instance, const uint8_t *data, uint32_t len)
{
    volatile uint32_t *tx_cfg;
    volatile uint32_t *tx_addr;
    volatile uint32_t *tx_size;
    volatile uint32_t *status;
    uint32_t i;

    if (instance >= UART_INSTANCE_COUNT) {
        return UART_ERROR_INVALID;
    }

    if (data == NULL) {
        return UART_ERROR_NULL;
    }

    if ((len == 0u) || (len > UART_TX_BUF_SIZE)) {
        return UART_ERROR_SIZE;
    }

    if (s_uart_tx_busy[instance] != false) {
        return UART_ERROR_BUSY;
    }

    tx_cfg = uart_reg(instance, UDMA_TX_CFG_OFFSET);
    tx_addr = uart_reg(instance, UDMA_TX_SADDR_OFFSET);
    tx_size = uart_reg(instance, UDMA_TX_SIZE_OFFSET);
    status = uart_reg(instance, UDMA_UART_STATUS_OFFSET);

    if (((*tx_cfg & UDMA_CFG_EN) != 0u) ||
        ((*status & UART_STATUS_TX_BUSY) != 0u)) {
        return UART_ERROR_BUSY;
    }

    for (i = 0u; i < len; i++) {
        uart_tx_buf[i] = data[i];
    }

    s_uart_tx_busy[instance] = true;
    s_uart_tx_notified[instance] = false;

    *tx_addr = (uint32_t)(uintptr_t)uart_tx_buf;
    *tx_size = len;
    *tx_cfg = UDMA_CFG_EN | UDMA_CFG_SIZE_8;
    memory_fence();

    return UART_OK;
}

/**
 * @brief Check whether an asynchronous UART transmit has completed.
 *
 * Completion means both the UDMA TX channel is disabled and the UART TX
 * shifter is no longer busy. On first completion detection, the transmit
 * complete hook is called.
 *
 * @param[in] instance UART peripheral index. Valid range: 0-3.
 *
 * @return true if the transfer is complete or no transfer is active.
 * @return false if the transfer is still active.
 *
 * @req REQ-DABAO-UART-0007
 */
bool uart_write_done(uint instance)
{
    volatile uint32_t *tx_cfg;
    volatile uint32_t *status;
    bool done;

    SEVS_ASSERT(instance < UART_INSTANCE_COUNT);

    if (instance >= UART_INSTANCE_COUNT) {
        return true;
    }

    if (s_uart_tx_busy[instance] == false) {
        return true;
    }

    tx_cfg = uart_reg(instance, UDMA_TX_CFG_OFFSET);
    status = uart_reg(instance, UDMA_UART_STATUS_OFFSET);

    done = (((*tx_cfg & UDMA_CFG_EN) == 0u) &&
            ((*status & UART_STATUS_TX_BUSY) == 0u));

    if (done != false) {
        s_uart_tx_busy[instance] = false;

        if (s_uart_tx_notified[instance] == false) {
            s_uart_tx_notified[instance] = true;
            uart_tx_complete_hook(instance, UART_OK);
        }

        return true;
    }

    return false;
}

/**
 * @brief Wait for an asynchronous UART transmit to complete.
 *
 * This function uses a bounded polling loop.
 *
 * @param[in] instance UART peripheral index. Valid range: 0-3.
 * @param[in] timeout_count Maximum number of polling attempts.
 *
 * @return UART_OK if the transfer completed.
 * @return UART_ERROR_INVALID if instance is invalid.
 * @return UART_ERROR_TIMEOUT if the timeout expired.
 *
 * @req REQ-DABAO-UART-0008
 */
int uart_write_wait(uint instance, uint32_t timeout_count)
{
    uint32_t i;

    if (instance >= UART_INSTANCE_COUNT) {
        return UART_ERROR_INVALID;
    }

    for (i = 0u; i < timeout_count; i++) {
        if (uart_write_done(instance) != false) {
            return UART_OK;
        }
    }

    return UART_ERROR_TIMEOUT;
}