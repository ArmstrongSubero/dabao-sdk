/*
 * pmu.c - Power management and clock configuration
 *
 * Reads the CGU and PMU registers to display the current clock
 * configuration and power state. Demonstrates reading the clock
 * tree that boot0/boot1 configured.
 *
 * Wiring:
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/regs/pmu.h"



int main(void)
{
    bao_init();

    mini_printf("\r\nPower Management Demo\r\n\r\n");

    mini_printf("Clock Generation Unit (CGU):\r\n");
    mini_printf("  CGU_LP:      0x%08x\r\n", CGU_LP);
    mini_printf("  CGU_SEL0:    0x%08x\r\n", CGU_SEL0);
    mini_printf("  CGU_SEL1:    0x%08x\r\n", CGU_SEL1);
    mini_printf("  CGU_FD_FCLK: 0x%08x\r\n", CGU_FD_FCLK);
    mini_printf("  CGU_FD_ACLK: 0x%08x\r\n", CGU_FD_ACLK);
    mini_printf("\r\n");

    mini_printf("Power Management Unit (PMU):\r\n");
    mini_printf("  PMU_CR:      0x%08x\r\n", PMU_CR);
    mini_printf("  PMU_SR:      0x%08x\r\n", PMU_SR);
    mini_printf("\r\n");

    mini_printf("Derived clocks:\r\n");
    mini_printf("  ACLK:  %u Hz (CPU)\r\n", ACLK_HZ);
    mini_printf("  FCLK:  %u Hz (BIO)\r\n", FCLK_HZ);
    mini_printf("  PCLK:  %u Hz (PWM ref)\r\n", ACLK_HZ / 8);
    mini_printf("  perclk: %u Hz (UART/SPI/I2C)\r\n", uart_get_perclk());

    while (1) {
        __asm__ volatile ("wfi");
    }
}
