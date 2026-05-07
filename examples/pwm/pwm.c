/*
 * pwm.c - LED breathing on PB1
 *
 * Fades an LED connected to PB1 using PWM slice 1, channel 1.
 * The LED smoothly ramps up and down in brightness.
 *
 * Wiring:
 *   PB1 (header pin 29) --> LED --> 330R --> GND
 *   PB14 --> USB-serial RX (for serial output)
 */

/*
 * Note: Duty cycle values of 0% and 100% may produce glitches on
 * this hardware. The breathing loop uses 1-99% to avoid this.
 */

#include "bao.h"
#include "hardware/pwm.h"

int main(void)
{
    bao_init();

    mini_printf("\r\nPWM LED Breathing on PB1\r\n");

    /* Set PB1 to PWM mode (AF3 = PWM1 channel 1) */
    pwm_init_pin(GPIO_PORT_B, 1);

    /* Initialize PWM slice 1 at 1 kHz */
    uint16_t period = pwm_init(1, 1000);
    mini_printf("PWM period: %u counts\r\n", period);

    while (1) {
       
     for (uint32_t pct = 1; pct <= 99; pct++) {
    pwm_set_percent(1, 1, pct);
    delay_ms(20);
}
delay_ms(200);
for (int32_t pct = 99; pct >= 1; pct--) {
    pwm_set_percent(1, 1, (uint32_t)pct);
    delay_ms(20);
}
delay_ms(200);
   }
}
