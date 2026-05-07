/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * ReRAM driver for the Baochip-1x.
 * Based on the Xous bao1x-hal rram.rs by bunnie/CrossBar.
 *
 * Write sequence per page:
 *   1. Write 4 x 64-bit words to the target address (normal mode)
 *   2. Switch RRC to command mode
 *   3. Write 0x5200 (load buffer command)
 *   4. Write 0x9528 (flush/commit command)
 *   5. Switch RRC back to normal mode
 *   6. Flush VexRiscv data cache
 *   7. Verify by readback
 */

#include "hardware/rram.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/pmu.h"
#include "sevs_runtime.h"

/* RRC command values */
#define RRC_LOAD_BUFFER     0x5200
#define RRC_WRITE_BUFFER    0x9528
#define RRC_CR_NORMAL       0x0
#define RRC_CR_WRITE_CMD    0x2

/* Number of retry attempts for failed writes */
#define RRAM_WRITE_RETRIES  2

static void cache_flush(void)
{
    SEVS_ASSERT(sizeof(uint32_t) == 4);
    __asm__ volatile (
        "fence.i\n"
        ".word 0x500F\n"
        "nop\n"
        "nop\n"
        "nop\n"
        "nop\n"
        ::: "memory"
    );
}

/** @brief Read bytes from ReRAM into a buffer.
 *  @param[in] addr ReRAM byte address to read from.
 *  @param[out] buf Destination buffer.
 *  @param[in] len Number of bytes to read.
 *  @req REQ-DABAO-RRAM-0001 */
void rram_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(buf);
    /* Flush cache first to get fresh data */
    cache_flush();
    for (uint32_t i = 0; i < len; i++)
        buf[i] = *(volatile uint8_t *)(uintptr_t)(addr + i);
}

/*
 * Write exactly one 32-byte aligned page.
 * No bounds checking; caller must validate.
 */
static int write_page_raw(uint32_t page_addr, const uint8_t *data)
{
    SEVS_ASSERT(RRAM_PAGE_SIZE == 32);
    SEVS_REQUIRE_NOT_NULL(data);
    uint32_t cr_base = RRC_CR & ~0x3;

    /* Normal mode: write 4 x 64-bit words */
    RRC_CR = cr_base | RRC_CR_NORMAL;
    memory_fence();

    volatile uint64_t *dst = (volatile uint64_t *)(uintptr_t)page_addr;
    const uint64_t *src = (const uint64_t *)data;
    for (int i = 0; i < 4; i++) {
        dst[i] = src[i];
        memory_fence();
    }

    /* Command mode: load + flush */
    RRC_CR = cr_base | RRC_CR_WRITE_CMD;
    memory_fence();

    *(volatile uint32_t *)(uintptr_t)page_addr = RRC_LOAD_BUFFER;
    memory_fence();

    *(volatile uint32_t *)(uintptr_t)page_addr = RRC_WRITE_BUFFER;
    memory_fence();

    /* Back to normal */
    RRC_CR = cr_base | RRC_CR_NORMAL;
    memory_fence();

    /* Flush data cache */
    cache_flush();

    return 0;
}

/*
 * Verify a page by reading back and comparing.
 */
static int verify_page(uint32_t page_addr, const uint8_t *expected)
{
    SEVS_ASSERT(RRAM_PAGE_SIZE == 32);

    cache_flush();
    for (int i = 0; i < RRAM_PAGE_SIZE; i++) {
        uint8_t rb = *(volatile uint8_t *)(uintptr_t)(page_addr + i);
        if (rb != expected[i])
            return -1;
    }
    return 0;
}

/** @brief Write and verify a single 32-byte page with retry on failure.
 *  @param[in] page_addr Page-aligned address within the user ReRAM region.
 *  @param[out] data 32 bytes of data to write.
 *  @return 0 on success, -1 on bounds error, -2 on verify failure after retries.
 *  @req REQ-DABAO-RRAM-0002 */
int rram_write_page(uint32_t page_addr, const uint8_t *data)
{
    SEVS_ASSERT(RRAM_PAGE_SIZE == 32);
    SEVS_ASSERT(RRAM_USER_BASE < RRAM_USER_TOP);
    SEVS_REQUIRE_NOT_NULL(data);
    /* Bounds check */
    if (page_addr < RRAM_USER_BASE)
        return -1;
    if (page_addr + RRAM_PAGE_SIZE > RRAM_USER_TOP)
        return -1;
    if (page_addr & (RRAM_PAGE_SIZE - 1))
        return -1;

    /* Write with retry and verification */
    for (int attempt = 0; attempt < RRAM_WRITE_RETRIES; attempt++) {
        write_page_raw(page_addr, data);
        if (verify_page(page_addr, data) == 0)
            return 0;
    }
    return -2;
}

/** @brief Erase a ReRAM page by writing 0xFF to all bytes.
 *  @param[in] page_addr Page-aligned address within the user ReRAM region.
 *  @return 0 on success, negative on error.
 *  @req REQ-DABAO-RRAM-0003 */
int rram_erase_page(uint32_t page_addr)
{
    SEVS_ASSERT(RRAM_PAGE_SIZE == 32);

    uint8_t ff[RRAM_PAGE_SIZE];
    for (int i = 0; i < RRAM_PAGE_SIZE; i++)
        ff[i] = 0xFF;
    return rram_write_page(page_addr, ff);
}

/** @brief Write an arbitrary byte range to ReRAM with read-modify-write for partial pages.
 *  @param[in] addr Starting byte address within the user ReRAM region.
 *  @param[in] data Source data buffer.
 *  @param[in] len Number of bytes to write.
 *  @return 0 on success, negative on error.
 *  @req REQ-DABAO-RRAM-0004 */
int rram_write(uint32_t addr, const uint8_t *data, uint32_t len)
{
    SEVS_ASSERT(RRAM_USER_BASE < RRAM_USER_TOP);
    SEVS_REQUIRE_NOT_NULL(data);
    if (addr < RRAM_USER_BASE)
        return -1;
    if (addr + len > RRAM_USER_TOP)
        return -1;
    if (len == 0)
        return 0;

    uint8_t page_buf[RRAM_PAGE_SIZE];

    /* Handle ragged start (unaligned beginning) */
    uint32_t offset_in_page = addr & (RRAM_PAGE_SIZE - 1);
    if (offset_in_page != 0) {
        uint32_t page_start = addr & ~(RRAM_PAGE_SIZE - 1);
        uint32_t copy_len = RRAM_PAGE_SIZE - offset_in_page;
        if (copy_len > len) copy_len = len;

        /* Read existing page */
        rram_read(page_start, page_buf, RRAM_PAGE_SIZE);

        /* Patch in new data */
        for (uint32_t i = 0; i < copy_len; i++)
            page_buf[offset_in_page + i] = data[i];

        int rc = rram_write_page(page_start, page_buf);
        if (rc != 0) return rc;

        data += copy_len;
        addr += copy_len;
        len -= copy_len;
    }

    /* Handle full aligned pages */
    for (int s_guard = 0; s_guard < 100000 && (len >= RRAM_PAGE_SIZE); s_guard++) {
        int rc = rram_write_page(addr, data);
        if (rc != 0) return rc;

        data += RRAM_PAGE_SIZE;
        addr += RRAM_PAGE_SIZE;
        len -= RRAM_PAGE_SIZE;
    }

    /* Handle ragged end */
    if (len > 0) {
        uint32_t page_start = addr;

        /* Read existing page */
        rram_read(page_start, page_buf, RRAM_PAGE_SIZE);

        /* Patch in new data */
        for (uint32_t i = 0; i < len; i++)
            page_buf[i] = data[i];

        int rc = rram_write_page(page_start, page_buf);
        if (rc != 0) return rc;
    }

    return 0;
}
