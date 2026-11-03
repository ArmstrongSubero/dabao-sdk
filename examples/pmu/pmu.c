/*
 * pmu.c - Clock configuration summary
 *
 * Prints the clock tree boot0/boot1 configured, from the SDK's own
 * constants and the UART divider it measured.
 *
 * This example used to read the CGU and PMU registers at 0x40040000
 * and 0x40060000 and print them. Those blocks are part of the boot
 * chain register region, and a program that did nothing but read a
 * register in that region (the RRC at 0x40000000) destroyed a board.
 * Application code must not touch 0x4000xxxx, 0x4004xxxx or 0x4006xxxx,
 * not even to read, so the register dump is gone. Everything below
 * comes from constants and from the UART2 setup word the boot ROM
 * left behind, which lives in the UDMA block and is safe to read.
 *
 * Wiring:
 *   PB14 --> USB-serial RX
 */

#include "bao.h"



int main(void)
{
    bao_init();

    mini_printf("\r\nClock Summary\r\n\r\n");

    mini_printf("Clocks set by boot0/boot1 (SDK constants):\r\n");
    mini_printf("  ACLK:   %u Hz (CPU, Timer0, TickTimer)\r\n", ACLK_HZ);
    mini_printf("  FCLK:   %u Hz (BIO)\r\n", FCLK_HZ);
    mini_printf("  PCLK:   %u Hz (PWM reference, ACLK / 8)\r\n", ACLK_HZ / 8);
    mini_printf("  perclk: %u Hz (UART, SPI, I2C; measured from UART2 setup)\r\n",
                uart_get_perclk());
    mini_printf("  cycle counter: %u cycles per us (measured)\r\n", cycles_per_us());
    mini_printf("\r\n");

    mini_printf("The CGU, PMU, AON, RTC, WDT and ATIMER registers are not read\r\n");
    mini_printf("here. They share the boot chain register region and even a\r\n");
    mini_printf("read there can permanently damage the board.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
