/*
 * bio_dma.c - Memory-to-memory DMA using BIO Core 3
 *
 * Demonstrates hardware memory copy at 700 MHz using the BIO
 * coprocessor as a DMA engine. Copies test data, verifies, and
 * compares timing against CPU memcpy.
 *
 * NOTE: BIO Core 3 is reserved for DMA and unavailable for
 * user BIO programs while DMA is active.
 *
 * Wiring:
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/bio_dma.h"

extern void *memcpy(void *dst, const void *src, unsigned int n);

#define TEST_SIZE   1024

static uint8_t src_buf[TEST_SIZE] __attribute__((aligned(4)));
static uint8_t dst_buf[TEST_SIZE] __attribute__((aligned(4)));

int main(void)
{
    bao_init();

    mini_printf("\r\n========================================\r\n");
    mini_printf("  BIO DMA Memory Copy Demo\r\n");
    mini_printf("========================================\r\n\r\n");

    bio_dma_init();
    mini_printf("[Init] BIO Core 3 loaded with DMA program\r\n");
    mini_printf("  src_buf at 0x%08x\r\n", (uint32_t)(uintptr_t)src_buf);
    mini_printf("  dst_buf at 0x%08x\r\n\r\n", (uint32_t)(uintptr_t)dst_buf);

    /* Fill source with test pattern using volatile writes
     * (bypasses CPU data cache so BIO can read from bus) */
    for (int i = 0; i < TEST_SIZE; i++)
        ((volatile uint8_t *)src_buf)[i] = (uint8_t)(i & 0xFF);

    /* Test 1: Blocking BIO DMA memcpy */
    for (int i = 0; i < TEST_SIZE; i++)
        ((volatile uint8_t *)dst_buf)[i] = 0;

    mini_printf("[Test 1] BIO DMA memcpy (%u bytes)...\r\n", TEST_SIZE);
    uint64_t t0 = millis();
    bio_dma_memcpy(dst_buf, src_buf, TEST_SIZE);
    uint64_t t1 = millis();

    int errors = 0;
    for (int i = 0; i < TEST_SIZE; i++) {
        if (((volatile uint8_t *)dst_buf)[i] != ((volatile uint8_t *)src_buf)[i]) errors++;
    }
    mini_printf("  Time: %u ms, Errors: %d\r\n", (uint32_t)(t1 - t0), errors);
    mini_printf("  src first 16: ");
    for (int i = 0; i < 16; i++)
        mini_printf("%02x ", ((volatile uint8_t *)src_buf)[i]);
    mini_printf("\r\n");
    mini_printf("  dst first 16: ");
    for (int i = 0; i < 16; i++)
        mini_printf("%02x ", ((volatile uint8_t *)dst_buf)[i]);
    mini_printf("\r\n");
    mini_printf("  EVENT_STATUS: 0x%08x\r\n", *(volatile uint32_t *)(0x50124040UL));
    mini_printf("  BIO CTRL:     0x%08x\r\n", *(volatile uint32_t *)(0x50124000UL));
    mini_printf("\r\n");

    /* Test 2: CPU memcpy for comparison */
    for (int i = 0; i < TEST_SIZE; i++)
        dst_buf[i] = 0;

    mini_printf("[Test 2] CPU memcpy (%u bytes)...\r\n", TEST_SIZE);
    t0 = millis();
    memcpy(dst_buf, src_buf, TEST_SIZE);
    t1 = millis();

    errors = 0;
    for (int i = 0; i < TEST_SIZE; i++) {
        if (dst_buf[i] != src_buf[i]) errors++;
    }
    mini_printf("  Time: %u ms, Errors: %d\r\n\r\n", (uint32_t)(t1 - t0), errors);

    /* Test 3: Non-blocking DMA */
    for (int i = 0; i < TEST_SIZE; i++)
        dst_buf[i] = 0;

    mini_printf("[Test 3] Non-blocking BIO DMA...\r\n");
    bio_dma_start(dst_buf, src_buf, TEST_SIZE);
    mini_printf("  Transfer started, waiting...\r\n");
    bio_dma_wait();

    errors = 0;
    for (int i = 0; i < TEST_SIZE; i++) {
        if (dst_buf[i] != src_buf[i]) errors++;
    }
    mini_printf("  Complete. Errors: %d\r\n\r\n", errors);

    /* Test 4: Large transfer */
    mini_printf("[Test 4] Multiple 1K transfers...\r\n");
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < TEST_SIZE; i++)
            src_buf[i] = (uint8_t)((i + round * 37) & 0xFF);
        for (int i = 0; i < TEST_SIZE; i++)
            dst_buf[i] = 0;

        bio_dma_memcpy(dst_buf, src_buf, TEST_SIZE);

        errors = 0;
        for (int i = 0; i < TEST_SIZE; i++) {
            if (dst_buf[i] != src_buf[i]) errors++;
        }
        mini_printf("  Round %d: Errors=%d\r\n", round + 1, errors);
    }
    mini_printf("\r\n");

    mini_printf("[Done] BIO DMA tests complete.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
