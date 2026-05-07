/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * W25Q external NOR flash driver for the Baochip-1x.
 *
 * Sits on top of the QSPI driver to provide read, write, and erase
 * operations for Winbond W25Q-series flash (W25Q128, W25Q64, etc.).
 * Supports both standard SPI and quad-mode data transfers.
 *
 * The driver handles page programming with automatic page-boundary
 * splitting, write enable sequencing, and busy-wait polling.
 *
 * Usage:
 *   w25q_init(1, 0, 10);     // SPIM1, CS0, clkdiv=10
 *   w25q_read_id(&mfr, &dev);
 *   w25q_read(0x000000, buf, 256);
 *   w25q_erase_sector(0x010000);
 *   w25q_write(0x010000, data, 128);
 */

#ifndef _HARDWARE_W25Q_H
#define _HARDWARE_W25Q_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Flash geometry (W25Q128FV defaults) */
#define W25Q_PAGE_SIZE          256
#define W25Q_SECTOR_SIZE        4096        /* 4 KB sector erase */
#define W25Q_BLOCK_SIZE_32K     0x8000      /* 32 KB block erase */
#define W25Q_BLOCK_SIZE_64K     0x10000     /* 64 KB block erase */

/* Status register bits */
#define W25Q_SR1_BUSY           0x01
#define W25Q_SR1_WEL            0x02
#define W25Q_SR2_QE             0x02        /* Quad Enable in SR2 */

/*
 * Initialize the W25Q flash.
 * Calls qspi_init() internally, resets the flash chip, and
 * verifies communication by reading the JEDEC ID.
 *
 * instance: SPIM instance (1 for Dabao QSPI port)
 * cs: chip select (0 or 1)
 * clkdiv: SPI clock divider
 *
 * Returns 0 on success, negative on failure.
 */
int w25q_init(uint instance, uint8_t cs, uint8_t clkdiv);

/*
 * Read the manufacturer ID and device ID.
 * mfr: receives manufacturer byte (0xEF for Winbond)
 * dev: receives device ID byte
 */
void w25q_read_id(uint8_t *mfr, uint8_t *dev);

/*
 * Read the 3-byte JEDEC ID (manufacturer, memory type, capacity).
 * id: receives 3 bytes
 */
void w25q_read_jedec_id(uint8_t *id);

/*
 * Read data from flash.
 * Uses standard SPI read (0x03) or quad output fast read (0x6B)
 * depending on whether quad mode is enabled.
 *
 * addr: 24-bit flash address
 * buf: destination buffer
 * len: number of bytes to read (no size limit)
 */
void w25q_read(uint32_t addr, uint8_t *buf, uint32_t len);

/*
 * Write data to flash.
 * Handles page-boundary splitting, write enable, and busy polling
 * automatically. The target region must be erased first.
 *
 * addr: 24-bit flash address
 * data: source buffer
 * len: number of bytes to write (no size limit)
 *
 * Returns 0 on success, negative on failure.
 */
int w25q_write(uint32_t addr, const uint8_t *data, uint32_t len);

/*
 * Erase a 4 KB sector.
 * addr: any address within the sector to erase.
 * Returns 0 on success, negative on failure.
 */
int w25q_erase_sector(uint32_t addr);

/*
 * Erase a 64 KB block.
 * addr: any address within the block to erase.
 * Returns 0 on success, negative on failure.
 */
int w25q_erase_block(uint32_t addr);

/*
 * Erase the entire chip.
 * This takes a long time (up to 200 seconds for W25Q128).
 * Returns 0 on success, negative on failure.
 */
int w25q_erase_chip(void);

/*
 * Read status register 1.
 */
uint8_t w25q_read_sr1(void);

/*
 * Read status register 2.
 */
uint8_t w25q_read_sr2(void);

/*
 * Enable quad I/O mode by setting the QE bit in status register 2.
 * Once enabled, quad-mode reads and writes use all 4 data lines.
 * Returns 0 on success, negative on failure.
 */
int w25q_enable_quad(void);

/*
 * Check if the flash is busy (write/erase in progress).
 */
bool w25q_is_busy(void);

/*
 * Block until the flash is no longer busy.
 */
void w25q_wait_busy(void);

#ifdef __cplusplus
}
#endif

#endif
