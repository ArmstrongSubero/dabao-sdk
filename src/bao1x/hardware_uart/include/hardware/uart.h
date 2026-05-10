/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART API for the Baochip-1x UDMA UART.
 *
 * The Baochip-1x has 4 UART instances (0-3) on the UDMA subsystem.
 * DMA buffers must reside in IFRAM, not SRAM. The driver allocates
 * a static TX staging buffer in IFRAM.
 *
 * UART2 is the only console on the Dabao board (PB13=RX, PB14=TX).
 */

#ifndef _HARDWARE_UART_H
#define _HARDWARE_UART_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UART_OK                    0
#define UART_ERROR_INVALID        -1
#define UART_ERROR_NULL           -2
#define UART_ERROR_SIZE           -3
#define UART_ERROR_BUSY           -4
#define UART_ERROR_TIMEOUT        -5

/*
 * Initialize a UART instance. Detects perclk from boot1's configuration,
 * opens the UDMA clock gate, and configures for 8N1.
 *
 * Returns the actual baud rate achieved.
 */
uint32_t uart_init(uint instance, uint32_t baudrate);

/* Send a single character. Blocks until the transmit completes. */
void uart_putc(uint instance, char c);

/* Send a null-terminated string. */
void uart_puts(uint instance, const char *s);

/* Send raw bytes. Blocks until all bytes are sent. */
void uart_write(uint instance, const uint8_t *data, uint32_t len);

/* Check if a received byte is available in polling mode. */
bool uart_is_readable(uint instance);

/*
 * Read a single byte in polling mode.
 *
 * This function uses a bounded poll. It returns 0 if no byte arrives before
 * the internal timeout expires.
 */
char uart_getc(uint instance);

/* Get the detected peripheral clock frequency. */
uint32_t uart_get_perclk(void);

/*
 * Start an asynchronous UART write.
 *
 * The data is copied into an internal IFRAM staging buffer before this
 * function returns. The caller's data buffer does not need to remain valid
 * after this call.
 *
 * This simple async API supports one transfer of up to 256 bytes at a time.
 * Only one async write per instance may be active at a time.
 *
 * When completion is detected by uart_write_done() or uart_write_wait(),
 * uart_tx_complete_hook() is called once.
 *
 * Returns:
 *   UART_OK             transfer started
 *   UART_ERROR_INVALID  invalid UART instance
 *   UART_ERROR_NULL     data is NULL
 *   UART_ERROR_SIZE     len is 0 or greater than 256
 *   UART_ERROR_BUSY     UART/UDMA is already busy
 */
int uart_write_async(uint instance, const uint8_t *data, uint32_t len);

/*
 * Check whether the last asynchronous UART write has completed.
 *
 * Completion means both the UDMA TX channel is disabled and the UART TX
 * shifter is no longer busy.
 *
 * Returns true if the transfer is complete or no transfer is active.
 * Returns false if the transfer is still active.
 */
bool uart_write_done(uint instance);

/*
 * Wait for the last asynchronous UART write to complete.
 *
 * This function uses a bounded polling loop to avoid unbounded waits.
 *
 * timeout_count is the maximum number of polling attempts.
 *
 * Returns:
 *   UART_OK             transfer completed
 *   UART_ERROR_INVALID  invalid UART instance
 *   UART_ERROR_TIMEOUT  timeout expired
 */
int uart_write_wait(uint instance, uint32_t timeout_count);

/*
 * UART transmit-complete hook.
 *
 * The default implementation in uart.c does nothing. Applications may override
 * this function by defining their own non-weak uart_tx_complete_hook().
 *
 * Do not call blocking UART functions or mini_printf() from this hook if the
 * hook is later moved to interrupt context.
 */
void uart_tx_complete_hook(uint instance, int status);

#ifdef __cplusplus
}
#endif

#endif