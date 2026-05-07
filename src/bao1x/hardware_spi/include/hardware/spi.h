/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * SPI master API for the Baochip-1x UDMA SPI.
 *
 * The chip has 4 SPI master instances (SPIM0-SPIM3).
 * On the Dabao board, SPI2 is broken out:
 *   PC0 = SPI2_CLK (AF2)
 *   PC1 = SPI2_D0/MOSI (AF2)
 *   PC2 = SPI2_D1/MISO (AF2)
 *   PC3 = SPI2_CS0 (AF2) or use a GPIO for software CS
 *
 * The UDMA SPI uses a command-based protocol: you build a command
 * sequence (CFG, SOT, TX_DATA, RX_DATA, EOT) in an IFRAM buffer
 * and the DMA engine executes them in order.
 */

#ifndef _HARDWARE_SPI_H
#define _HARDWARE_SPI_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize an SPI master instance.
 * Sets up the UDMA clock gate and pin muxing.
 * For instance 2 (Dabao default), configures PC0-PC2 as AF2.
 */
void spi_init(uint instance);

/*
 * Write len bytes over SPI. No data is read back.
 *
 * clkdiv: SPI clock = perclk / (2 * (clkdiv + 1))
 *   clkdiv=4  -> ~10 MHz
 *   clkdiv=49 -> ~1 MHz
 */
void spi_write_blocking(uint instance, const uint8_t *data, uint32_t len, uint8_t clkdiv);

/*
 * Full-duplex SPI transfer. Writes tx and simultaneously reads into rx.
 * Either tx or rx may be NULL for write-only or read-only transfers.
 */
void spi_write_read_blocking(uint instance, const uint8_t *tx, uint8_t *rx,
                             uint32_t len, uint8_t clkdiv);

/* SPI command builder macros (for advanced use) */
#define SPI_CMD_CFG(clkdiv, cpol, cpha) \
    ((0 << 28) | ((cpol) << 9) | ((cpha) << 8) | ((clkdiv) & 0xFF))
#define SPI_CMD_SOT(cs) \
    ((1 << 28) | ((cs) & 0x3))
#define SPI_CMD_TX_DATA(words, bitsword, lsbfirst) \
    ((6 << 28) | ((lsbfirst) << 26) | (((bitsword) - 1) << 16) | (((words) - 1) & 0xFFFF))
#define SPI_CMD_RX_DATA(words, bitsword, lsbfirst) \
    ((7 << 28) | ((lsbfirst) << 26) | (((bitsword) - 1) << 16) | (((words) - 1) & 0xFFFF))
#define SPI_CMD_FULL_DUPLEX(words, bitsword, lsbfirst) \
    ((0xC << 28) | ((lsbfirst) << 26) | (((bitsword) - 1) << 16) | (((words) - 1) & 0xFFFF))
#define SPI_CMD_EOT(evt, cs_keep) \
    ((9 << 28) | ((evt) << 1) | ((cs_keep) & 1))

/*
 * Start an asynchronous full-duplex SPI transfer.
 * Kicks the UDMA and returns immediately.
 * tx: data to send (NULL for read-only, sends 0x00).
 * rx_len: number of bytes to receive (0 for write-only).
 * Use spi_async_done() to check completion, spi_async_read() to get RX data.
 * Only one async transfer per instance at a time.
 */
void spi_write_read_async(uint instance, const uint8_t *tx, uint32_t len,
                          uint8_t clkdiv);

/* Returns true when the last async transfer has completed. */
bool spi_async_done(uint instance);

/* Block until the last async transfer completes. */
void spi_async_wait(uint instance);

/* Copy received data from the internal RX buffer after async transfer. */
void spi_async_read(uint instance, uint8_t *rx, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
