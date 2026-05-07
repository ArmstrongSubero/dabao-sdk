/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * QSPI driver for the Baochip-1x UDMA SPI master.
 *
 * This is a general-purpose quad-SPI driver that sits on any SPIM
 * instance. It exposes the full UDMA command set including SEND_CMD,
 * DUMMY, and quad-mode TX/RX that the standard SPI driver omits.
 *
 * On the Dabao board, SPIM1 is the QSPI port with four data lines:
 *   PC11 = QSPI1_CLK   (AF1)
 *   PC7  = QSPI1_D0    (AF1)
 *   PC8  = QSPI1_D1    (AF1)
 *   PC9  = QSPI1_D2    (AF1)
 *   PC10 = QSPI1_D3    (AF1)
 *   PC12 = QSPI1_CS0   (AF1)
 *   PC13 = QSPI1_CS1   (AF1)
 *
 * Usage:
 *   qspi_init(1, 10);   // SPIM1, clkdiv=10 (~5 MHz)
 *   qspi_select(1, 0);  // assert CS0
 *   qspi_send_cmd(1, 0x9F);           // JEDEC ID (1-line)
 *   qspi_read_blocking(1, buf, 3, false);  // read 3 bytes (1-line)
 *   qspi_deselect(1);
 */

#ifndef _HARDWARE_QSPI_H
#define _HARDWARE_QSPI_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize SPIM instance for QSPI operation.
 * Sets up UDMA clock gate and pin muxing.
 * For instance 1 (Dabao QSPI port), configures PC7-PC13 as AF1.
 *
 * clkdiv: SPI clock = perclk / (2 * (clkdiv + 1))
 *   clkdiv=4  -> ~10 MHz
 *   clkdiv=9  -> ~5 MHz
 *   clkdiv=49 -> ~1 MHz
 */
void qspi_init(uint instance, uint8_t clkdiv);

/*
 * Assert chip select (active low).
 * cs: 0 or 1 for hardware CS0/CS1.
 */
void qspi_select(uint instance, uint8_t cs);

/*
 * Deassert chip select.
 */
void qspi_deselect(uint instance);

/*
 * Send a command byte (instruction) on a single SPI line.
 * This uses the SEND_CMD opcode (0x2) which always sends
 * on D0 only, regardless of quad mode.
 */
void qspi_send_cmd(uint instance, uint8_t cmd);

/*
 * Send a command byte in quad mode (all 4 data lines).
 * Used when the flash chip is in QPI mode where even
 * the instruction phase is quad.
 */
void qspi_send_cmd_quad(uint instance, uint8_t cmd);

/*
 * Insert dummy clock cycles.
 * cycles: number of dummy clocks (1-31).
 */
void qspi_dummy(uint instance, uint8_t cycles);

/*
 * Transmit data bytes.
 * quad: if true, send on 4 data lines; if false, single line.
 */
void qspi_write_blocking(uint instance, const uint8_t *data,
                          uint32_t len, bool quad);

/*
 * Receive data bytes.
 * quad: if true, receive on 4 data lines; if false, single line.
 */
void qspi_read_blocking(uint instance, uint8_t *data,
                         uint32_t len, bool quad);

/*
 * Execute a raw UDMA command list.
 * cmds: array of 32-bit UDMA SPI command words.
 * ncmds: number of command words (max 16).
 *
 * For advanced use when the helper functions above are not
 * flexible enough (e.g. combining address + dummy + data
 * in a single CS assertion).
 */
void qspi_exec_cmds(uint instance, const uint32_t *cmds, uint32_t ncmds);

/*
 * Check if the QSPI interface is busy.
 */
bool qspi_busy(uint instance);

/*
 * Wait until the QSPI interface is idle.
 */
void qspi_wait(uint instance);

/* ------------------------------------------------------------------ */
/* UDMA SPI command builder macros                                     */
/*                                                                     */
/* These cover the full command set from the RTL, including SEND_CMD    */
/* and DUMMY which the standard SPI driver does not expose.             */
/* ------------------------------------------------------------------ */

/* Configure clock divider, polarity, phase */
#define QSPI_CMD_CFG(clkdiv, cpol, cpha) \
    ((0x0 << 28) | ((cpol) << 9) | ((cpha) << 8) | ((clkdiv) & 0xFF))

/* Assert chip select */
#define QSPI_CMD_SOT(cs) \
    ((0x1 << 28) | ((cs) & 0x3))

/* Send command/instruction byte (bit 27 = quad flag) */
#define QSPI_CMD_SEND_CMD(quad, nbits, value) \
    ((0x2 << 28) | (((quad) ? 1 : 0) << 27) | ((((nbits) - 1) & 0x1F) << 16) | ((value) & 0xFFFF))

/* Insert dummy clock cycles */
#define QSPI_CMD_DUMMY(cycles) \
    ((0x4 << 28) | ((((cycles) - 1) & 0x1F) << 16))

/* Transmit data (bit 27 = quad flag) */
#define QSPI_CMD_TX_DATA(words, bitsword, quad, lsbfirst) \
    ((0x6 << 28) | (((quad) ? 1 : 0) << 27) | ((lsbfirst) << 26) \
     | (((bitsword) - 1) << 16) | (((words) - 1) & 0xFFFF))

/* Receive data (bit 27 = quad flag) */
#define QSPI_CMD_RX_DATA(words, bitsword, quad, lsbfirst) \
    ((0x7 << 28) | (((quad) ? 1 : 0) << 27) | ((lsbfirst) << 26) \
     | (((bitsword) - 1) << 16) | (((words) - 1) & 0xFFFF))

/* End transfer, deassert CS */
#define QSPI_CMD_EOT(evt, cs_keep) \
    ((0x9 << 28) | ((evt) << 1) | ((cs_keep) & 1))

/* Full duplex (always single-line, bit 27 is unused) */
#define QSPI_CMD_FULL_DUPLEX(words, bitsword, lsbfirst) \
    ((0xC << 28) | ((lsbfirst) << 26) \
     | (((bitsword) - 1) << 16) | (((words) - 1) & 0xFFFF))

/* Repeat next command N times */
#define QSPI_CMD_RPT(count) \
    ((0x8 << 28) | ((count) & 0xFFFF))

/* End repeat */
#define QSPI_CMD_RPT_END \
    (0xA << 28)

#ifdef __cplusplus
}
#endif

#endif
