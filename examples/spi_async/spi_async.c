/*
 * spi_async.c - Non-blocking SPI transfer demo
 *
 * Demonstrates async SPI: the CPU kicks off a full-duplex SPI DMA
 * transfer and does other work (toggles LED, counts cycles) while
 * the transfer runs in the background.
 *
 * This example does a loopback test: connect MOSI (PC1) to MISO (PC2)
 * with a jumper wire. Data sent out MOSI comes back on MISO.
 *
 * Wiring:
 *   PC0 = SPI2 CLK  (header pin 14)
 *   PC1 = SPI2 MOSI (header pin 12) --+
 *   PC2 = SPI2 MISO (header pin 11) --+ (loopback jumper)
 *   PB1 --> LED --> 330R --> GND
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/spi.h"

#define TEST_LEN    128
#define SPI_CLKDIV  4    /* ~10 MHz */

int main(void)
{
    bao_init();

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    mini_printf("\r\n========================================\r\n");
    mini_printf("  Async SPI Demo (loopback)\r\n");
    mini_printf("========================================\r\n\r\n");

    spi_init(2);

    /* Test 1: Blocking SPI full-duplex for comparison */
    uint8_t tx_data[TEST_LEN];
    uint8_t rx_data[TEST_LEN];

    for (int i = 0; i < TEST_LEN; i++)
        tx_data[i] = (uint8_t)(i & 0xFF);

    mini_printf("[Test 1] Blocking SPI write+read (%u bytes)...\r\n", TEST_LEN);
    uint64_t t0 = millis();
    spi_write_read_blocking(2, tx_data, rx_data, TEST_LEN, SPI_CLKDIV);
    uint64_t t1 = millis();

    int errors = 0;
    for (int i = 0; i < TEST_LEN; i++) {
        if (rx_data[i] != tx_data[i]) errors++;
    }
    mini_printf("  Time: %u ms, Errors: %d\r\n", (uint32_t)(t1 - t0), errors);
    mini_printf("  RX first 8: ");
    for (int i = 0; i < 8; i++)
        mini_printf("%02x ", rx_data[i]);
    mini_printf("\r\n\r\n");

    /* Test 2: Async SPI - CPU does work during transfer */
    for (int i = 0; i < TEST_LEN; i++)
        rx_data[i] = 0;

    mini_printf("[Test 2] Async SPI + LED work...\r\n");
    uint32_t toggles = 0;

    spi_write_read_async(2, tx_data, TEST_LEN, SPI_CLKDIV);

    while (!spi_async_done(2)) {
        gpio_toggle(GPIO_PORT_B, 1);
        toggles++;
    }

    spi_async_read(2, rx_data, TEST_LEN);

    errors = 0;
    for (int i = 0; i < TEST_LEN; i++) {
        if (rx_data[i] != tx_data[i]) errors++;
    }
    mini_printf("  LED toggled %u times during transfer\r\n", toggles);
    mini_printf("  Errors: %d\r\n", errors);
    mini_printf("  RX first 8: ");
    for (int i = 0; i < 8; i++)
        mini_printf("%02x ", rx_data[i]);
    mini_printf("\r\n\r\n");

    /* Test 3: Back-to-back async transfers */
    mini_printf("[Test 3] 5 back-to-back async transfers...\r\n");
    for (int round = 0; round < 5; round++) {
        for (int i = 0; i < TEST_LEN; i++)
            tx_data[i] = (uint8_t)((i + round * 17) & 0xFF);

        spi_write_read_async(2, tx_data, TEST_LEN, SPI_CLKDIV);
        spi_async_wait(2);
        spi_async_read(2, rx_data, TEST_LEN);

        errors = 0;
        for (int i = 0; i < TEST_LEN; i++) {
            if (rx_data[i] != tx_data[i]) errors++;
        }
        mini_printf("  Round %d: Errors=%d\r\n", round + 1, errors);
    }

    mini_printf("\r\n[Done] Async SPI demo complete.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
