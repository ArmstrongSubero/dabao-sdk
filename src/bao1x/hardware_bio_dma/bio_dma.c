/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * BIO DMA driver for the Baochip-1x.
 *
 * Uses BIO Core 3 as a memory-to-memory DMA engine. The program
 * running on Core 3 reads transfer parameters from FIFOs and
 * performs word-by-word copies at 700 MHz via the AHB bus.
 *
 * Based on the Xous xous-bio-bdma DMA implementation.
 */

#include "hardware/bio_dma.h"
#include "hardware/bio.h"
#include "sevs_runtime.h"

/*
 * BIO DMA program for Core 3.
 * Assembled from the Xous dma_basic_code:
 *
 *   20:
 *     mv  a3, x18         # src addr from FIFO2
 *     mv  a2, x17         # dst addr from FIFO1
 *     li  x29, 1          # clear event bit 0
 *     mv  a1, x16         # byte count from FIFO0
 *     sw  x0, 0(a2)       # pipeline sync
 *     add a4, a1, a3      # end = src + count
 *   30:
 *     lw  t0, 0(a3)       # load from src
 *     sw  t0, 0(a2)       # store to dst
 *     addi a3, a3, 4      # src += 4
 *     sw  t0, 0(a2)       # store again (pipeline sync)
 *     addi a2, a2, 4      # dst += 4
 *     bne a3, a4, 30b     # loop
 *     li  x28, 1          # set event bit 0 (done)
 *     j   20b             # wait for next transfer
 */
static const uint32_t bio_dma_code[] = {
    0x00090693,  /* 0x00: mv   a3, x18       */
    0x00088613,  /* 0x04: mv   a2, x17       */
    0x00100e93,  /* 0x08: li   x29, 1        */
    0x00080593,  /* 0x0c: mv   a1, x16       */
    0x00062023,  /* 0x10: sw   x0, 0(a2)     */
    0x00d58733,  /* 0x14: add  a4, a1, a3    */
    0x0006a283,  /* 0x18: lw   t0, 0(a3)     */
    0x00562023,  /* 0x1c: sw   t0, 0(a2)     */
    0x00468693,  /* 0x20: addi a3, a3, 4     */
    0x00562023,  /* 0x24: sw   t0, 0(a2)     */
    0x00460613,  /* 0x28: addi a2, a2, 4     */
    0xfee696e3,  /* 0x2c: bne  a3, a4, -20   */
    0x00100e13,  /* 0x30: li   x28, 1        */
    0xfcdff06f,  /* 0x34: j    -52           */
};

#define DMA_CORE    3

static bool s_dma_initialized = false;

/** @brief Load the DMA program onto BIO Core 3 and start it.
 *  @req REQ-DABAO-BIO-DMA-0001 */
void bio_dma_init(void)
{
    SEVS_ASSERT(DMA_CORE == 3);
    if (s_dma_initialized) return;

    /* Stop Core 3 only, preserve other cores' state */
    uint32_t ctrl = BIO_SFR_CTRL;
    ctrl &= ~BIO_CTRL_EN(DMA_CORE);
    BIO_SFR_CTRL = ctrl;
    memory_fence();

    /*
     * Disable BIO bus access filters.
     * By default, the BIO cores cannot access SRAM or peripherals.
     * Set DISABLE_FILTER_MEM (bit 7) and DISABLE_FILTER_PERI (bit 6)
     * in SFR_CONFIG to allow full bus access.
     */
    BIO_SFR_CONFIG |= (1 << 7) | (1 << 6);
    memory_fence();

    /* Load DMA program into Core 3's instruction memory */
    bio_load_code_words(DMA_CORE, bio_dma_code,
                        sizeof(bio_dma_code) / sizeof(bio_dma_code[0]));

    /* Run Core 3 at full speed (QDIV = 1.0 = 0x1_0000) */
    BIO_SFR_QDIV3 = 0x10000;

    /* Clear any pending events */
    BIO_SFR_EVENT_CLR = 0xFFFFFFFF;
    memory_fence();

    /* Clear FIFOs for Core 3 */
    BIO_SFR_FIFO_CLR = (1 << DMA_CORE);
    memory_fence();

    /* Start Core 3 without disturbing other cores */
    BIO_SFR_CTRL = BIO_SFR_CTRL | BIO_CTRL_START(DMA_CORE);
    memory_fence();

    s_dma_initialized = true;
}

/** @brief Begin a DMA transfer by writing src, dst, and length to the BIO FIFOs.
 *  @param[in] dst Destination address.
 *  @param[in] src Source address.
 *  @param[in] len Transfer length in bytes (must be word-aligned).
 *  @req REQ-DABAO-BIO-DMA-0002 */
void bio_dma_start(void *dst, const void *src, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(dst);
    SEVS_REQUIRE_NOT_NULL(src);
    if (!s_dma_initialized) bio_dma_init();
    if (len == 0) return;

    /*
     * Flush CPU data cache before DMA so the BIO core reads
     * actual source data from memory.
     */
    __asm__ volatile (
        "fence\n"
        ".word 0x500F\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        ::: "memory"
    );

    /*
     * Write transfer parameters to FIFOs.
     * Order matters: the BIO program reads TXF2 first (src),
     * then TXF1 (dst), then TXF0 (byte count, triggers the copy).
     */
    BIO_SFR_TXF2 = (uint32_t)src;
    BIO_SFR_TXF1 = (uint32_t)dst;
    BIO_SFR_TXF0 = len;
}

/** @brief Check whether a BIO DMA transfer is still in progress.
 *  @return true if the transfer has not yet completed.
 *  @req REQ-DABAO-BIO-DMA-0003 */
bool bio_dma_busy(void)
{
    return (BIO_SFR_EVENT_STATUS & 0x1) == 0;
}

/** @brief Block until the current BIO DMA transfer completes, then flush cache.
 *  @req REQ-DABAO-BIO-DMA-0004 */
void bio_dma_wait(void)
{
    /* Wait for event bit 0 (set by the BIO program when done) */
    for (int s_poll = 0; s_poll < 1000000 && (bio_dma_busy()); s_poll++) { /* bounded poll */ }

    /* Flush CPU data cache so reads see the DMA result */
    __asm__ volatile (
        "fence\n"
        ".word 0x500F\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        ::: "memory"
    );
}

/** @brief Copy memory using BIO DMA, blocking until done.
 *  @param[in] dst Destination address.
 *  @param[in] src Source address.
 *  @param[in] len Transfer length in bytes.
 *  @req REQ-DABAO-BIO-DMA-0004 */
void bio_dma_memcpy(void *dst, const void *src, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(dst);
    SEVS_REQUIRE_NOT_NULL(src);
    bio_dma_start(dst, src, len);
    bio_dma_wait();
}
