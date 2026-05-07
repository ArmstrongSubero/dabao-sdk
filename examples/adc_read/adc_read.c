/*
 * adc_read.c - Read analog voltage on PC9
 *
 * Reads the ADC channel connected to PC9 and the internal temperature
 * sensor, printing the raw values and computed voltages.
 *
 * Wiring:
 *   PC9 (header pin 7 area) --> voltage source (0 to 3.3V)
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/adc.h"

int main(void)
{
    bao_init();

    mini_printf("\r\nADC Demo\r\n");
    mini_printf("10-bit, Vref = %u mV (internal bandgap)\r\n\r\n", ADC_VREF_MV);

    adc_init();

    while (1) {
        uint32_t raw = adc_read_raw(0);
        uint32_t mv  = adc_raw_to_mv(raw);
        uint32_t temp_raw = adc_read_temp_raw();

        mini_printf("CH0: raw=%u  voltage=%u mV    TEMP: raw=%u\r\n",
                    raw, mv, temp_raw);

        delay_ms(500);
    }
}
