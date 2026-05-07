/*
 * rram.c - ReRAM non-volatile storage demo
 *
 * Demonstrates the on-chip ReRAM as persistent storage (like EEPROM).
 * Writes test data, reads it back, verifies, and shows that data
 * survives across power cycles.
 *
 * The driver handles unaligned addresses and arbitrary lengths.
 * Writes are verified by readback with automatic retry.
 *
 * Safe storage region: 0x60200000 - 0x603FFFFF (upper 2 MB).
 * The lower 2 MB is protected (contains boot0/boot1/firmware).
 *
 * Wiring:
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/rram.h"

#define TEST_ADDR   RRAM_USER_BASE

static void print_hex_buf(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        mini_printf("%02x ", data[i]);
}

int main(void)
{
    bao_init();

    mini_printf("\r\n========================================\r\n");
    mini_printf("  ReRAM Storage Demo\r\n");
    mini_printf("========================================\r\n");
    mini_printf("  RRAM: 0x%08x - 0x%08x (%u KB total)\r\n",
                RRAM_BASE, RRAM_BASE + RRAM_SIZE, RRAM_SIZE / 1024);
    mini_printf("  User: 0x%08x - 0x%08x (%u KB safe)\r\n",
                RRAM_USER_BASE, RRAM_USER_BASE + RRAM_USER_SIZE,
                RRAM_USER_SIZE / 1024);
    mini_printf("  Page: %u bytes\r\n\r\n", RRAM_PAGE_SIZE);

    uint8_t buf[64];

    /* Test 1: Read current contents (survives power cycles) */
    mini_printf("[Test 1] Read current contents at 0x%08x:\r\n", TEST_ADDR);
    rram_read(TEST_ADDR, buf, 32);
    mini_printf("  "); print_hex_buf(buf, 32); mini_printf("\r\n\r\n");

    /* Test 2: Aligned page write */
    mini_printf("[Test 2] Write 32 bytes (aligned page)...\r\n");
    for (int i = 0; i < 32; i++)
        buf[i] = (uint8_t)(i * 7 + 0x42);
    mini_printf("  Data: "); print_hex_buf(buf, 32); mini_printf("\r\n");

    int rc = rram_write_page(TEST_ADDR, buf);
    mini_printf("  Result: %d\r\n", rc);

    uint8_t rbuf[32];
    rram_read(TEST_ADDR, rbuf, 32);
    mini_printf("  Read:  "); print_hex_buf(rbuf, 32); mini_printf("\r\n");

    int match = 1;
    for (int i = 0; i < 32; i++)
        if (rbuf[i] != buf[i]) match = 0;
    mini_printf("  Verify: %s\r\n\r\n", match ? "PASS" : "FAIL");

    /* Test 3: Unaligned write (arbitrary offset and length) */
    mini_printf("[Test 3] Write 10 bytes at offset +5 (unaligned)...\r\n");
    uint8_t small[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE, 0x12, 0x34 };
    rc = rram_write(TEST_ADDR + 5, small, 10);
    mini_printf("  Result: %d\r\n", rc);

    rram_read(TEST_ADDR, rbuf, 32);
    mini_printf("  Full page: "); print_hex_buf(rbuf, 32); mini_printf("\r\n");
    mini_printf("  Bytes 5-14 should be: de ad be ef ca fe ba be 12 34\r\n\r\n");

    /* Test 4: Multi-page write */
    mini_printf("[Test 4] Write 48 bytes across page boundary...\r\n");
    uint8_t big[48];
    for (int i = 0; i < 48; i++)
        big[i] = (uint8_t)(0xA0 + i);

    rc = rram_write(TEST_ADDR + 32, big, 48);
    mini_printf("  Result: %d\r\n", rc);

    rram_read(TEST_ADDR + 32, rbuf, 32);
    mini_printf("  Page 1: "); print_hex_buf(rbuf, 32); mini_printf("\r\n");
    rram_read(TEST_ADDR + 64, rbuf, 16);
    mini_printf("  Page 2: "); print_hex_buf(rbuf, 16); mini_printf("\r\n\r\n");

    /* Test 5: Erase */
    mini_printf("[Test 5] Erase page...\r\n");
    rc = rram_erase_page(TEST_ADDR);
    mini_printf("  Result: %d\r\n", rc);
    rram_read(TEST_ADDR, rbuf, 32);
    mini_printf("  After:  "); print_hex_buf(rbuf, 32); mini_printf("\r\n\r\n");

    mini_printf("[Done] ReRAM demo complete.\r\n");
    mini_printf("Power cycle and run again to see data persists.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
