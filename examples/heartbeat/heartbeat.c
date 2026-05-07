/*
 * heartbeat.c - D11CTIME heartbeat timer
 *
 * The D11CTIME is the simplest timer on the Baochip-1x. Write an interval
 * (in ACLK cycles) to the CONTROL register. Bit 0 of HEARTBEAT toggles
 * each time the interval expires. No interrupt, just poll.
 *
 * This is useful as a low-cost periodic flag without needing interrupts.
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

    mini_printf("\r\nD11CTIME Heartbeat Demo\r\n\r\n");

    /*
     * Set the heartbeat interval.
     * ACLK = 350 MHz. For a 1 Hz toggle (0.5 Hz blink):
     *   interval = 350,000,000 cycles
     * For a 2 Hz toggle (1 Hz blink):
     *   interval = 175,000,000 cycles
     */
    uint32_t interval = ACLK_HZ / 2;  /* 2 Hz toggle = 1 Hz blink */
    D11CTIME_CONTROL = interval;

    mini_printf("Interval: %u ACLK cycles\r\n", interval);
    mini_printf("Expected toggle rate: 2 Hz\r\n\r\n");

    uint32_t last_beat = D11CTIME_HEARTBEAT & 1;
    uint32_t count = 0;

    while (1) {
        uint32_t beat = D11CTIME_HEARTBEAT & 1;
        if (beat != last_beat) {
            last_beat = beat;
            gpio_toggle(GPIO_PORT_B, 1);
            count++;
            mini_printf("beat %u (heartbeat=%u)\r\n", count, beat);
        }
    }
}
