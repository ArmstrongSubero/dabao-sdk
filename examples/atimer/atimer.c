/*
 * atimer.c - Always-on timer with compare match
 *
 * The ATimer has two channels (AT0, AT1) that count up from zero.
 * Each channel has a compare register; when the counter matches,
 * it can reset, generate an interrupt, or keep counting.
 *
 * AT0 and AT1 can cascade: AT1 clocks from AT0's overflow,
 * creating a 64-bit counter.
 *
 * This example runs AT0 as a free-running counter on PCLK (~44 MHz)
 * and measures elapsed time by reading the counter directly.
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

    mini_printf("\r\nATimer Demo\r\n\r\n");

    /* Reset AT0 */
    AT0_CFG = AT_CFG_RST;
    memory_fence();
    AT0_RESET = AT_RESET_RST;
    memory_fence();

    /* Configure AT0: PCLK, count up, no compare reset, repeat */
    AT0_CMP = 0xFFFFFFFF;
    AT0_CFG = AT_CFG_EN | AT_CFG_CLK_PCLK | AT_CFG_MODE_INC | AT_CFG_REPEAT;
    memory_fence();
    AT0_START = AT_START_EN;
    memory_fence();

    /* Measure PCLK speed by sampling AT0 over a known delay */
    mini_printf("Measuring AT0 clock speed...\r\n");
    uint32_t c0 = AT0_CNT;
    delay_ms(1000);
    uint32_t c1 = AT0_CNT;
    uint32_t ticks_per_sec = c1 - c0;
    mini_printf("  AT0 ticks in 1 second: %u\r\n", ticks_per_sec);
    mini_printf("  AT0 clock: ~%u MHz\r\n\r\n", ticks_per_sec / 1000000);

    /* Now use AT0 with compare-and-reset to generate a periodic event */
    mini_printf("Configuring AT0 with 500 ms compare reset...\r\n");

    AT0_CFG = AT_CFG_RST;
    memory_fence();
    AT0_RESET = AT_RESET_RST;
    memory_fence();

    /* Compare value for 500 ms: ticks_per_sec / 2 */
    uint32_t cmp_val = ticks_per_sec / 2;
    AT0_CMP = cmp_val;
    AT0_CFG = AT_CFG_EN | AT_CFG_CLK_PCLK | AT_CFG_MODE_RST | AT_CFG_REPEAT;
    memory_fence();
    AT0_START = AT_START_EN;
    memory_fence();

    mini_printf("  Compare value: %u\r\n", cmp_val);
    mini_printf("  LED toggles on each rollover:\r\n\r\n");

    uint32_t last_cnt = 0;
    uint32_t toggles = 0;
    while (1) {
        uint32_t cnt = AT0_CNT;
        /* Counter resets to 0 on compare match */
        if (cnt < last_cnt) {
            gpio_toggle(GPIO_PORT_B, 1);
            toggles++;
            mini_printf("  toggle %u (cnt reset detected)\r\n", toggles);
        }
        last_cnt = cnt;
    }
}
