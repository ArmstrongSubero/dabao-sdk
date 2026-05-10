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

#define UART_ASYNC_TIMEOUT_COUNT 1000000u

static volatile bool s_uart2_tx_complete = false;

void uart_tx_complete_hook(uint instance, int status)
{
    if ((instance == 2u) && (status == UART_OK)) {
        s_uart2_tx_complete = true;
    }
}

int main(void)
{
    uint64_t t0;
    uint64_t t1;
    uint32_t toggles;
    int result;

    bao_init();

    gpio_init(GPIO_PORT_B, 1u);
    gpio_set_dir(GPIO_PORT_B, 1u, true);

    mini_printf("\r\n========================================\r\n");
    mini_printf("  Non-blocking UART Demo\r\n");
    mini_printf("========================================\r\n\r\n");

    /* Test 1: Blocking write for comparison. */
    mini_printf("[Test 1] Blocking write...\r\n");

    t0 = millis();

    {
        const char *msg = "Hello from blocking UART write!\r\n";
        uart_write(2u, (const uint8_t *)msg, 32u);
    }

    t1 = millis();

    mini_printf("  Blocking took %u ms\r\n\r\n", (uint32_t)(t1 - t0));

    /* Test 2: Async write - CPU is free during transmit. */
    mini_printf("[Test 2] Async write + LED work...\r\n");

    toggles = 0u;
    s_uart2_tx_complete = false;

    {
        const char *async_msg = "This was sent asynchronously!\r\n";

        result = uart_write_async(2u, (const uint8_t *)async_msg, 30u);

        if (result != UART_OK) {
            mini_printf("  FAIL: uart_write_async result=%d\r\n", result);
            while (1) {
                __asm__ volatile ("wfi");
            }
        }
    }

    /*
     * Do other work while UART transmits.
     * This loop is bounded for SEVS/JPL-style compliance.
     */
    {
        uint32_t guard;

        for (guard = 0u; guard < UART_ASYNC_TIMEOUT_COUNT; guard++) {
            if (uart_write_done(2u) != false) {
                break;
            }

            gpio_toggle(GPIO_PORT_B, 1u);
            toggles++;
        }

        if (guard >= UART_ASYNC_TIMEOUT_COUNT) {
            mini_printf("  FAIL: async TX timeout\r\n");
            while (1) {
                __asm__ volatile ("wfi");
            }
        }
    }

    if (s_uart2_tx_complete == false) {
        mini_printf("  FAIL: TX complete hook not called\r\n");
        while (1) {
            __asm__ volatile ("wfi");
        }
    }

    delay_ms(10u);
    mini_printf("  LED toggled %u times during async TX\r\n\r\n", toggles);

    /* Test 3: Multiple async writes in sequence. */
    mini_printf("[Test 3] Sequential async writes...\r\n");

    {
        uint32_t i;

        for (i = 0u; i < 5u; i++) {
            char buf[32];
            uint32_t len;

            len = 0u;
            buf[len++] = ' ';
            buf[len++] = ' ';
            buf[len++] = 'M';
            buf[len++] = 's';
            buf[len++] = 'g';
            buf[len++] = ' ';
            buf[len++] = (char)('0' + i);
            buf[len++] = '\r';
            buf[len++] = '\n';

            s_uart2_tx_complete = false;

            result = uart_write_async(2u, (const uint8_t *)buf, len);

            if (result != UART_OK) {
                mini_printf("  FAIL: uart_write_async result=%d\r\n", result);
                while (1) {
                    __asm__ volatile ("wfi");
                }
            }

            result = uart_write_wait(2u, UART_ASYNC_TIMEOUT_COUNT);

            if (result != UART_OK) {
                mini_printf("  FAIL: uart_write_wait result=%d\r\n", result);
                while (1) {
                    __asm__ volatile ("wfi");
                }
            }

            if (s_uart2_tx_complete == false) {
                mini_printf("  FAIL: TX complete hook not called\r\n");
                while (1) {
                    __asm__ volatile ("wfi");
                }
            }
        }
    }

    delay_ms(10u);
    mini_printf("\r\n[Done] Async UART demo complete.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }

    return 0;
}