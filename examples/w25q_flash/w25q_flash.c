/*
 * w25q_flash.c - W25Q external NOR flash demo
 *
 * Demonstrates read, erase, write, and verify on an external
 * W25Q-series flash chip connected to the Dabao QSPI1 port.
 *
 * Wiring:
 *   PC11 (QSPI1_CLK)  --> W25Q CLK
 *   PC7  (QSPI1_D0)   --> W25Q DI (MOSI)
 *   PC8  (QSPI1_D1)   --> W25Q DO (MISO)
 *   PC9  (QSPI1_D2)   --> W25Q /WP  (or IO2 for quad)
 *   PC10 (QSPI1_D3)   --> W25Q /HOLD (or IO3 for quad)
 *   PC12 (QSPI1_CS0)  --> W25Q /CS
 *   3V3                --> W25Q VCC
 *   GND                --> W25Q GND
 *   PB14               --> USB-serial RX (for UART output)
 */

#include "bao.h"
#include "hardware/qspi.h"
#include "hardware/w25q.h"

#define TEST_ADDR   0x010000    /* sector 16 (avoid sector 0) */
#define TEST_LEN    64

int main(void)
{
    bao_init();
    mini_printf("\r\n=== W25Q Flash Test ===\r\n");

    /* Initialize flash on SPIM1, CS0, ~5 MHz */
    int rc = w25q_init(1, 0, 9);
    if (rc != 0)
    {
        mini_printf("w25q_init failed: %d\r\n", rc);
        mini_printf("Check wiring and power.\r\n");
        while (1) {}
    }
    mini_printf("Flash initialized.\r\n");

    /* Read and display JEDEC ID */
    uint8_t id[3];
    w25q_read_jedec_id(id);
    mini_printf("JEDEC ID: %02x %02x %02x\r\n", id[0], id[1], id[2]);

    if (id[0] == 0xEF)
        mini_printf("  Manufacturer: Winbond\r\n");
    else
        mini_printf("  Manufacturer: 0x%02x\r\n", id[0]);

    /* Read manufacturer/device ID */
    uint8_t mfr, dev;
    w25q_read_id(&mfr, &dev);
    mini_printf("Mfr/Dev ID: %02x / %02x\r\n", mfr, dev);

    /* Read status registers */
    mini_printf("SR1: 0x%02x  SR2: 0x%02x\r\n",
                w25q_read_sr1(), w25q_read_sr2());

    /* Enable quad mode */
    mini_printf("\r\nEnabling quad mode... ");
    rc = w25q_enable_quad();
    if (rc == 0)
        mini_printf("OK (SR2: 0x%02x)\r\n", w25q_read_sr2());
    else
        mini_printf("FAILED (%d)\r\n", rc);

    /* Erase test sector */
    mini_printf("\r\nErasing sector at 0x%06x... ", TEST_ADDR);
    rc = w25q_erase_sector(TEST_ADDR);
    if (rc == 0)
        mini_printf("OK\r\n");
    else
    {
        mini_printf("FAILED (%d)\r\n", rc);
        while (1) {}
    }

    /* Verify erase (all 0xFF) */
    uint8_t buf[TEST_LEN];
    w25q_read(TEST_ADDR, buf, TEST_LEN);
    bool erase_ok = true;
    for (int i = 0; i < TEST_LEN; i++)
    {
        if (buf[i] != 0xFF)
        {
            erase_ok = false;
            break;
        }
    }
    mini_printf("Erase verify: %s\r\n", erase_ok ? "PASS" : "FAIL");

    /* Write test pattern */
    uint8_t pattern[TEST_LEN];
    for (int i = 0; i < TEST_LEN; i++)
        pattern[i] = (uint8_t)(i & 0xFF);

    mini_printf("Writing %d bytes at 0x%06x... ", TEST_LEN, TEST_ADDR);
    rc = w25q_write(TEST_ADDR, pattern, TEST_LEN);
    if (rc == 0)
        mini_printf("OK\r\n");
    else
    {
        mini_printf("FAILED (%d)\r\n", rc);
        while (1) {}
    }

    /* Read back and verify */
    for (int i = 0; i < TEST_LEN; i++)
        buf[i] = 0;

    w25q_read(TEST_ADDR, buf, TEST_LEN);

    mini_printf("Read back:\r\n");
    for (int i = 0; i < TEST_LEN; i++)
    {
        mini_printf("%02x ", buf[i]);
        if ((i & 0xF) == 0xF)
            mini_printf("\r\n");
    }

    bool verify_ok = true;
    for (int i = 0; i < TEST_LEN; i++)
    {
        if (buf[i] != pattern[i])
        {
            mini_printf("MISMATCH at offset %d: expected %02x, got %02x\r\n",
                        i, pattern[i], buf[i]);
            verify_ok = false;
        }
    }
    mini_printf("\r\nVerify: %s\r\n", verify_ok ? "PASS" : "FAIL");

    mini_printf("\r\n=== Test Complete ===\r\n");
    while (1) {}
}
