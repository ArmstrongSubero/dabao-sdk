/*
 * hello_uart.c - Serial output on UART2
 *
 * Prints a message over UART2 (PB14=TX, PB13=RX) and blinks the LED.
 * Connect a USB-serial adapter to see the output at 115200 baud, 8N1.
 *
 * Wiring:
 *   PB14 --> USB-serial RX
 *   PB13 --> USB-serial TX (for future receive, not used here)
 *   GND  --> USB-serial GND
 *   PB1  --> LED --> 330R --> GND
 */

#include "bao.h"

int main(void)
{
    bao_init();

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    mini_printf("\r\nHello from Dabao!\r\n");
    mini_printf("UART2 running at %u baud\r\n", BAO_DEFAULT_UART_BAUD_RATE);
    mini_printf("Peripheral clock: %u Hz\r\n", uart_get_perclk());

    uint32_t count = 0;
    while (1) {
        mini_printf("count = %u\r\n", count++);
        gpio_toggle(GPIO_PORT_B, 1);
        delay_ms(1000);
    }
}
