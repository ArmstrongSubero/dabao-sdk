/*
 * button.c - Read a push button on PB3
 *
 * Reads a button connected between PB3 and GND. The internal pull-up
 * holds the pin high when the button is not pressed. Pressing the button
 * pulls it low. The LED on PB1 mirrors the button state.
 *
 * This demonstrates gpio_get(), gpio_pull_up(), and input configuration.
 *
 * Wiring:
 *   PB3 (header pin 32) --> one leg of push button
 *   GND                 --> other leg of push button
 *   PB1 (header pin 29) --> LED --> 330R --> GND
 *   PB14                --> USB-serial RX (for serial output)
 *   GND                 --> USB-serial GND
 */

#include "bao.h"

int main(void)
{
    bao_init();

    /* LED on PB1 */
    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);
    gpio_put(GPIO_PORT_B, 1, false);

    /* Button on PB3: input with pull-up */
    gpio_init(GPIO_PORT_B, 3);
    gpio_set_dir(GPIO_PORT_B, 3, false);
    gpio_pull_up(GPIO_PORT_B, 3);

    mini_printf("\r\nButton test on PB3\r\n");
    mini_printf("Press the button to light the LED on PB1.\r\n\r\n");

    bool last_state = true;

    while (1) {
        /* Read button: low = pressed (pulled to GND), high = released (pull-up) */
        bool pressed = !gpio_get(GPIO_PORT_B, 3);

        /* Mirror to LED */
        gpio_put(GPIO_PORT_B, 1, pressed);

        /* Print on state change */
        if (pressed != last_state) {
            if (pressed)
                mini_printf("Button pressed\r\n");
            else
                mini_printf("Button released\r\n");
            last_state = pressed;
        }

        delay_ms(20);  /* Simple debounce */
    }
}
