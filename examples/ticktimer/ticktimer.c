/*
 * ticktimer.c - 64-bit millisecond counter
 *
 * The TickTimer is a free-running 64-bit counter clocked from ACLK.
 * It increments once per CLOCKS_PER_TICK cycles, giving a millisecond
 * resolution timebase. This is what delay_ms() and millis() use internally.
 *
 * This example shows direct register access and demonstrates the
 * MSLEEP_TARGET alarm feature.
 *
 * Wiring:
 *   PB1  --> LED --> 330R --> GND
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/regs/timer.h"


int main(void)
{
    bao_init();

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    mini_printf("\r\nTickTimer Demo\r\n");
    mini_printf("CLOCKS_PER_TICK = %u (ACLK/%u = 1 ms)\r\n\r\n",
                TICKTIMER_CLOCKS_PER_TICK, TICKTIMER_CLOCKS_PER_TICK);

    /* Read the 64-bit counter directly */
    mini_printf("Elapsed milliseconds:\r\n");

    for (int i = 0; i < 20; i++) {
        uint64_t ms = millis();
        uint32_t lo = (uint32_t)(ms & 0xFFFFFFFF);
        uint32_t hi = (uint32_t)(ms >> 32);

        mini_printf("  %u:%08u ms\r\n", hi, lo);
        gpio_toggle(GPIO_PORT_B, 1);
        delay_ms(500);
    }

    /* Demonstrate timing measurement */
    mini_printf("\r\nTiming a 1-second delay:\r\n");
    uint64_t t0 = millis();
    delay_ms(1000);
    uint64_t t1 = millis();
    mini_printf("  Measured: %u ms (expected 1000)\r\n", (uint32_t)(t1 - t0));

    while (1) {
        __asm__ volatile ("wfi");
    }
}
