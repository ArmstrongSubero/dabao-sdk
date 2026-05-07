/*
 * rtc.c - Real-time clock demo
 *
 * Initializes the RTC, sets the time to zero, and prints
 * the elapsed seconds continuously.
 *
 * Wiring:
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/rtc.h"

int main(void)
{
    bao_init();

    mini_printf("\r\nRTC Demo\r\n\r\n");

    rtc_init();
    rtc_set_time(0);
    rtc_start();

    mini_printf("RTC started. Counting seconds:\r\n");

    while (1) {
        uint32_t t = rtc_get_time();
        uint32_t hours   = t / 3600;
        uint32_t minutes = (t % 3600) / 60;
        uint32_t seconds = t % 60;

        mini_printf("  %02u:%02u:%02u  (raw: %u)\r\n",
                    hours, minutes, seconds, t);

        delay_ms(1000);
    }
}
