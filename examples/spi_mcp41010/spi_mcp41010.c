/*
 * spi_mcp41010.c - MCP41010 digital potentiometer over SPI
 *
 * Sweeps the wiper of an MCP41010 10K digital pot up and down.
 * Measure the voltage on the wiper pin (PW0) with a multimeter.
 *
 * The UDMA SPI handles clocking automatically. CS is toggled
 * manually via a GPIO pin since the MCP41010 needs CS to latch
 * the command on the rising edge.
 *
 * Wiring:
 *   PC0 (header pin 14) --> SCK  (MCP pin 2)
 *   PC1 (header pin 12) --> SI   (MCP pin 3)
 *   PB4 (header pin 34) --> CS   (MCP pin 1)
 *   3V3                 --> VDD  (MCP pin 8)
 *   GND                 --> VSS  (MCP pin 4)
 *   3V3                 --> PA0  (MCP pin 5)
 *   GND                 --> PB0  (MCP pin 7)
 *   Measure             --> PW0  (MCP pin 6)
 */

#include "bao.h"
#include "hardware/spi.h"

#define CS_PORT  GPIO_PORT_B
#define CS_PIN   4
#define MCP41010_CMD_WRITE  0x11

static void cs_low(void)  { gpio_put(CS_PORT, CS_PIN, false); }
static void cs_high(void) { gpio_put(CS_PORT, CS_PIN, true);  }

static void mcp41010_set_wiper(uint8_t value)
{
    uint8_t pkt[2] = { MCP41010_CMD_WRITE, value };

    cs_low();
    delay_us(10);
    spi_write_blocking(2, pkt, 2, 49);  /* ~1 MHz */
    delay_us(10);
    cs_high();
    delay_us(10);
}

int main(void)
{
    bao_init();

    mini_printf("\r\nMCP41010 Digital Pot Demo\r\n");

    /* SPI2 on PC0-PC2 */
    spi_init(2);

    /* Software CS on PB4 */
    gpio_init(CS_PORT, CS_PIN);
    gpio_set_dir(CS_PORT, CS_PIN, true);
    cs_high();

    /* LED on PB1 */
    gpio_init(GPIO_PORT_B, 1);
    gpio_set_dir(GPIO_PORT_B, 1, true);

    uint32_t cycle = 0;
    while (1) {
        cycle++;
        mini_printf("Cycle %u: sweeping up\r\n", cycle);
        for (uint32_t i = 0; i <= 255; i++) {
            mcp41010_set_wiper((uint8_t)i);
            delay_ms(10);
        }

        mini_printf("Cycle %u: sweeping down\r\n", cycle);
        for (int32_t i = 255; i >= 0; i--) {
            mcp41010_set_wiper((uint8_t)i);
            delay_ms(10);
        }

        gpio_toggle(GPIO_PORT_B, 1);
    }
}
