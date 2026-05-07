/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * ReRAM (RRAM) non-volatile storage driver for the Baochip-1x.
 *
 * The Baochip-1x has 4 MB of on-chip ReRAM at 0x60000000. It retains
 * data across power cycles and can be read as ordinary memory (just
 * dereference a pointer). Writes require a special command sequence
 * through the RRC controller and a VexRiscv data cache flush.
 *
 * The lower portion of RRAM contains boot0, boot1, and user firmware.
 * This driver restricts writes to the upper 2 MB (0x60200000 and above)
 * to prevent accidentally bricking the boot sequence.
 *
 * Write granularity is 32 bytes (one page). The driver handles
 * arbitrary addresses and lengths by doing read-modify-write on
 * partial pages. Every write is verified by readback.
 */

#ifndef _HARDWARE_RRAM_H
#define _HARDWARE_RRAM_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RRAM_BASE           0x60000000UL
#define RRAM_SIZE           0x00400000UL    /* 4 MB */
#define RRAM_PAGE_SIZE      32              /* bytes per page */

/*
 * Safe storage region. Writes outside this range are rejected.
 * Floor: 1 MB above firmware load address (0x60060000 + 0x100000).
 * Ceiling: below security vectors at top of RRAM.
 */
#define RRAM_USER_BASE      0x60160000UL
#define RRAM_USER_TOP       0x603DA000UL    /* security vectors above this */
#define RRAM_USER_SIZE      (RRAM_USER_TOP - RRAM_USER_BASE)  /* ~2.5 MB */

/*
 * Read bytes from RRAM. This is just a memcpy from the RRAM address
 * space, but goes through volatile reads to bypass the data cache.
 */
void rram_read(uint32_t addr, uint8_t *buf, uint32_t len);

/*
 * Write bytes to RRAM at any address and length.
 * Handles unaligned addresses and non-page-sized lengths by doing
 * read-modify-write on partial pages. Verifies each page by readback.
 *
 * addr: absolute RRAM address (must be >= RRAM_USER_BASE)
 * data: source buffer
 * len:  number of bytes to write
 *
 * Returns 0 on success, -1 if address is out of bounds, -2 if
 * verification failed after retries.
 */
int rram_write(uint32_t addr, const uint8_t *data, uint32_t len);

/*
 * Write a single 32-byte page. The address must be 32-byte aligned
 * and within the user storage region.
 *
 * Returns 0 on success, -1 on error.
 */
int rram_write_page(uint32_t page_addr, const uint8_t *data);

/*
 * Erase a page by writing all 0xFF. RRAM does not require a separate
 * erase step (any value can be written at any time), but this is
 * provided for compatibility with EEPROM-style usage patterns.
 */
int rram_erase_page(uint32_t page_addr);

#ifdef __cplusplus
}
#endif

#endif
