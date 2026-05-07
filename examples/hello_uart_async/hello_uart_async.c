/*
 * hello_uart_async.c - Non-blocking UART transmit demo
 *
 * Demonstrates uart_write_async: the CPU kicks off a UART DMA
 * transfer and immediately continues doing other work (toggling
 * an LED) while the data transmits in the background.
 *
 * Wiring:
 *   PB1  --> LED --> 330R --> GND
 *   PB14 --> USB-serial RX
 */

#include "bao.h"

int main(void)
{
    bao_init();

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    mini_printf("\r\n========================================\r\n");
    mini_printf("  Non-blocking UART Demo\r\n");
    mini_printf("========================================\r\n\r\n");

    /* Test 1: Blocking write for comparison */
    mini_printf("[Test 1] Blocking write...\r\n");
    uint64_t t0 = millis();
    const char *msg = "Hello from blocking UART write!\r\n";
    uart_write(2, (const uint8_t *)msg, 32);
    uint64_t t1 = millis();
    mini_printf("  Blocking took %u ms\r\n\r\n", (uint32_t)(t1 - t0));

    /* Test 2: Async write - CPU is free during transmit */
    mini_printf("[Test 2] Async write + LED work...\r\n");

    const char *async_msg = "This was sent asynchronously!\r\n";
    uint32_t toggles = 0;

    uart_write_async(2, (const uint8_t *)async_msg, 30);

    /* Do other work while UART transmits */
    while (!uart_write_done(2)) {
        gpio_toggle(GPIO_PORT_B, 1);
        toggles++;
    }

    /* Wait a moment, then report */
    delay_ms(10);
    mini_printf("  LED toggled %u times during async TX\r\n\r\n", toggles);

    /* Test 3: Multiple async writes in sequence */
    mini_printf("[Test 3] Sequential async writes...\r\n");

    for (int i = 0; i < 5; i++) {
        char buf[32];
        int len = 0;
        buf[len++] = ' '; buf[len++] = ' ';
        buf[len++] = 'M'; buf[len++] = 's'; buf[len++] = 'g'; buf[len++] = ' ';
        buf[len++] = '0' + i;
        buf[len++] = '\r'; buf[len++] = '\n';

        uart_write_async(2, (const uint8_t *)buf, len);
        uart_write_wait(2);
    }

    delay_ms(10);
    mini_printf("\r\n[Done] Async UART demo complete.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
