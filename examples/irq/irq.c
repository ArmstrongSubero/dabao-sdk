/*
 * irq.c - Interrupt dispatch system demo
 *
 * Demonstrates the IRQ driver with two interrupt sources:
 *   - Timer0: toggles LED every 500 ms
 *   - UART2 TX done: prints a message when each UART transmit completes
 *
 * Both handlers are registered via irq_set_handler() and dispatched
 * automatically by the IRQ driver's trap_dispatch.
 *
 * Wiring:
 *   PB1  --> LED --> 330R --> GND
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/irq.h"
#include "hardware/timer.h"

static volatile uint32_t timer_count = 0;
static volatile uint32_t uart_tx_count = 0;

/* Timer0 interrupt handler */
static void on_timer0(uint32_t pending)
{
    (void)pending;
    gpio_toggle(GPIO_PORT_B, 1);
    timer_count++;
}

/* UART interrupt handler (IRQARRAY5) */
static void on_uart(uint32_t pending)
{
    if (pending & UART2_EVT_TX)
        uart_tx_count++;
}

int main(void)
{
    bao_init();

    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    mini_printf("\r\n========================================\r\n");
    mini_printf("  IRQ Dispatch Demo\r\n");
    mini_printf("========================================\r\n\r\n");

    /* Initialize the IRQ dispatch system */
    irq_init();

    /* Register Timer0 handler and start periodic interrupt */
    irq_set_handler(IRQ_TIMER0, on_timer0);
    timer0_start_periodic_ms(500);
    mini_printf("[Init] Timer0: 500 ms periodic, LED on PB1\r\n");

    /* Register UART handler for TX complete events */
    irq_set_handler(IRQ_UART, on_uart);
    irq_enable_events(IRQ_UART, UART2_EVT_TX);
    mini_printf("[Init] UART2 TX done interrupt enabled\r\n\r\n");

    /* Main loop: print status, interrupts handle the rest */
    uint32_t last_timer = 0;
    for (int i = 0; i < 20; i++) {
        /* Wait for next timer tick */
        while (timer_count == last_timer) {}
        last_timer = timer_count;

        mini_printf("  tick %u, uart_tx_count=%u\r\n",
                    timer_count, uart_tx_count);
    }

    mini_printf("\r\n[Done] IRQ dispatch demo complete.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
