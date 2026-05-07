/*
 * bio_blink.c - GPIO square wave driven by the BIO coprocessor
 *
 * Loads a simple toggle program onto BIO core 0 and generates
 * a 100 kHz square wave on PB2, verifiable with an oscilloscope.
 * The main CPU is free to do other work while the BIO runs.
 *
 * Wiring:
 *   PB2 (header pin 31) --> scope probe
 *   GND                 --> scope ground
 *   PB14                --> USB-serial RX
 */

#include "bao.h"
#include "hardware/bio.h"

/*
 * BIO program: toggles BIO pin 2 (PB2) with quantum timing.
 *
 *   addi  x5, x0, 4       ; bit mask for pin 2
 *   addi  x26, x5, 0      ; GPIO mask = pin 2
 *   addi  x24, x5, 0      ; pin 2 = output
 * loop:
 *   addi  x22, x5, 0      ; set pin 2 HIGH
 *   addi  x20, x0, 0      ; wait quantum
 *   addi  x23, x0, 0      ; clear pin 2 LOW
 *   addi  x20, x0, 0      ; wait quantum
 *   jal   x0, -16          ; loop
 */
static const uint32_t bio_blink_program[] = {
    0x00400293,   /* addi  x5, x0, 4    */
    0x00028D13,   /* addi  x26, x5, 0   */
    0x00028C13,   /* addi  x24, x5, 0   */
    0x00028B13,   /* addi  x22, x5, 0   */
    0x00000A13,   /* addi  x20, x0, 0   */
    0x00000B93,   /* addi  x23, x0, 0   */
    0x00000A13,   /* addi  x20, x0, 0   */
    0xFF1FF06F,   /* jal   x0, -16      */
};

int main(void)
{
    bao_init();

    mini_printf("\r\nBIO Square Wave on PB2\r\n");

    bio_init(FCLK_HZ);
    bio_load_code_words(0, bio_blink_program, 8);
    bio_map_pin(2);
    bio_set_divider(0, 1750, 0);  /* 100 kHz output (fclk / 1750 / 4) */
    bio_start_cores(0x1);

    mini_printf("BIO core 0 running. Expected: 100 kHz on PB2.\r\n");

    /* Main CPU is free */
    uint32_t count = 0;
    while (1) {
        if (!bio_core_trapped(0)) {
            mini_printf("  [%u] BIO running, PC=0x%03x\r\n",
                        count++, bio_core_pc(0));
        } else {
            mini_printf("  [%u] BIO TRAPPED!\r\n", count++);
        }
        delay_ms(2000);
    }
}
