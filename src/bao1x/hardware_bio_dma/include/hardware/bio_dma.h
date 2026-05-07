/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * BIO DMA - memory-to-memory DMA using BIO Core 3.
 *
 * The Baochip-1x PL230 MDMA controller is not writable from bare
 * metal code. Instead, we use BIO Core 3 as a DMA engine. The BIO
 * cores run at 700 MHz and have AHB bus master access to all memory
 * regions (SRAM, IFRAM, RRAM, peripherals).
 *
 * A small program is loaded onto Core 3 that reads source address,
 * destination address, and byte count from the FIFO, performs a
 * word-by-word copy, then signals completion via an event flag.
 *
 * NOTE: Using BIO DMA reserves Core 3. It is unavailable for user
 * BIO programs while DMA is active.
 */

#ifndef _HARDWARE_BIO_DMA_H
#define _HARDWARE_BIO_DMA_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Initialize BIO Core 3 as a DMA engine.
 * Loads the DMA copy program and starts the core.
 * Must be called once before any bio_dma transfers.
 * Core 3 is reserved after this call.
 */
void bio_dma_init(void);

/*
 * Blocking memory-to-memory copy using BIO Core 3.
 * Both src and dst must be word-aligned.
 * len must be a multiple of 4.
 * Flushes the CPU data cache after the transfer.
 */
void bio_dma_memcpy(void *dst, const void *src, uint32_t len);

/*
 * Start a non-blocking DMA transfer.
 * Both src and dst must be word-aligned.
 * len must be a multiple of 4.
 * Returns immediately. Use bio_dma_busy() or bio_dma_wait().
 */
void bio_dma_start(void *dst, const void *src, uint32_t len);

/*
 * Returns true if a transfer is still in progress.
 */
bool bio_dma_busy(void);

/*
 * Block until the current transfer completes.
 * Flushes the CPU data cache.
 */
void bio_dma_wait(void);

#ifdef __cplusplus
}
#endif

#endif
