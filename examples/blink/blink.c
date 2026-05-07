/*
 * blink.c - LED blink on PB1
 *
 * The simplest Dabao program. Blinks an LED connected to PB1 at 1 Hz.
 *
 * Wiring:
 *   PB1 (header pin 29) --> LED anode --> 330 ohm resistor --> GND
 */

#include "bao.h"

int main(void)
{
    bao_init();
    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);
    while (1) {
        gpio_toggle(GPIO_PORT_B, 1);
        delay_ms(500);
    }
}