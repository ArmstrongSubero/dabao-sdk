/*
 * wdt.c - Watchdog timer demo
 *
 * The watchdog counts down from a loaded value. If the counter
 * reaches zero before your code feeds it, the chip resets.
 *
 * This demo starts with a large load value to observe the countdown
 * speed, feeds the watchdog for 10 cycles, then stops feeding.
 * The chip will reset and you'll see the banner print again.
 *
 * Wiring:
 *   PB1 (header pin 29) --> LED --> 330R --> GND
 *   PB14 --> USB-serial RX
 */

/*
 * WDT clock is derived from the core clock via the fdpclk divider.
 * Empirically measured at ~11.4 MHz on Dabao hardware.
 * wdt_start() timeouts are approximate. For precise timing,
 * use wdt_start_raw() with a calibrated counter value.
 */

#include "bao.h"
#include "hardware/wdt.h"

int main(void)
{
    bao_init();

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    mini_printf("\r\n");
    mini_printf("========================================\r\n");
    mini_printf("  Watchdog Timer Demo\r\n");
    mini_printf("========================================\r\n\r\n");

    /* Read registers before enabling */
    mini_printf("[Test 1] WDT state before enable:\r\n");
    mini_printf("  VAL: 0x%08x\r\n", wdt_get_count());
    mini_printf("\r\n");

    /* Start with a large load value to observe countdown speed */
    uint32_t load = 0x10000000;
    mini_printf("[Test 2] Starting watchdog (load=0x%08x)...\r\n", load);
    wdt_start_raw(load);

    /* Sample the counter to see how fast it counts down */
    for (int i = 0; i < 5; i++) {
        mini_printf("  CNT: 0x%08x\r\n", wdt_get_count());
        delay_ms(500);
    }
    mini_printf("\r\n");

    /* Feed and verify counter resets */
    mini_printf("[Test 3] Feeding watchdog...\r\n");
    wdt_feed();
    mini_printf("  CNT after feed: 0x%08x\r\n", wdt_get_count());
    mini_printf("\r\n");

    /* Feed loop */
    mini_printf("[Test 4] Feed loop (10 cycles, 500 ms each)...\r\n");
    for (int i = 0; i < 10; i++) {
        wdt_feed();
        mini_printf("  Feed %d: CNT=0x%08x\r\n", i + 1, wdt_get_count());
        gpio_toggle(GPIO_PORT_B, 1);
        delay_ms(500);
    }
    mini_printf("\r\n");

    /* Stop feeding. The chip will reset when the counter hits zero. */
    mini_printf("[Test 5] STOPPING FEED. Watchdog will reset chip.\r\n");
    mini_printf("  If you see the banner again, reset worked.\r\n\r\n");

    uint64_t last_print = 0;
    while (1) {
        uint64_t now = millis();
        if (now - last_print >= 1000) {
            last_print = now;
            mini_printf("  CNT: 0x%08x\r\n", wdt_get_count());
        }
    }
}
