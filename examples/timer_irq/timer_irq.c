/*
 * timer_irq.c - LED blink driven by Timer0 interrupt
 *
 * Configures Timer0 to fire an interrupt every 500 ms.
 * The interrupt handler toggles the LED on PB1.
 * The main loop is free to do other work.
 *
 * Uses the IRQ dispatch driver for interrupt handling.
 *
 * Wiring:
 *   PB1 (header pin 29) --> LED --> 330R --> GND
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

static volatile uint32_t timer_ticks = 0;

static void on_timer0(uint32_t pending)
{
    (void)pending;
    gpio_toggle(GPIO_PORT_B, 1);
    timer_ticks++;
}

int main(void)
{
    bao_init();

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    mini_printf("\r\nTimer Interrupt Demo\r\n");
    mini_printf("LED on PB1 toggles every 500 ms via Timer0 IRQ\r\n\r\n");

    irq_init();
    irq_set_handler(IRQ_TIMER0, on_timer0);
    timer0_start_periodic_ms(500);

    uint32_t last = 0;
    while (1) {
        if (timer_ticks != last) {
            last = timer_ticks;
            mini_printf("tick %u\r\n", last);
        }
    }
}
