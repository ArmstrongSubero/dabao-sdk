/*
 * trng.c - Hardware random number generator
 *
 * Generates random numbers using the SCE TRNG and prints them over UART.
 */

#include "bao.h"
#include "hardware/trng.h"

int main(void)
{
    bao_init();

    mini_printf("\r\nTRNG Demo\r\n\r\n");

    trng_init();

    /* Generate 8 random words */
    uint32_t rng[8];
    int rc = trng_generate(rng, 8);
    mini_printf("Generate 8 words: %s\r\n", (rc == 0) ? "OK" : "FAIL");
    if (rc == 0) {
        for (int i = 0; i < 8; i++)
            mini_printf("  [%d] 0x%08x\r\n", i, rng[i]);
    }

    /* Continuous output */
    mini_printf("\r\nContinuous random output:\r\n");
    while (1) {
        uint32_t r[4];
        rc = trng_generate(r, 4);
        if (rc == 0)
            mini_printf("%08x %08x %08x %08x\r\n", r[0], r[1], r[2], r[3]);
        delay_ms(500);
    }
}
