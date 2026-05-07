/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART API for the Baochip-1x UDMA UART.
 *
 * The Baochip-1x has 4 UART instances (0-3) on the UDMA subsystem.
 * DMA buffers must reside in IFRAM, not SRAM. The driver allocates
 * a static TX buffer in IFRAM for each initialized instance.
 *
 * UART2 is the default console on the Dabao board (PB13=RX, PB14=TX).
 */

#ifndef _HARDWARE_UART_H
#define _HARDWARE_UART_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

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

/* Check if a received byte is available (polling mode). */
bool uart_is_readable(uint instance);

/* Read a single byte (polling mode). Blocks until data arrives. */
char uart_getc(uint instance);

/* Get the detected peripheral clock frequency (valid after uart_init). */
uint32_t uart_get_perclk(void);

/*
 * Start an asynchronous write. Kicks the UDMA transfer and returns
 * immediately. Use uart_write_done() to check completion.
 * The data buffer must remain valid until the transfer completes.
 * Only one async write per instance at a time.
 */
void uart_write_async(uint instance, const uint8_t *data, uint32_t len);

/* Returns true when the last async write has completed. */
bool uart_write_done(uint instance);

/* Block until the last async write completes. */
void uart_write_wait(uint instance);

#ifdef __cplusplus
}
#endif

#endif
