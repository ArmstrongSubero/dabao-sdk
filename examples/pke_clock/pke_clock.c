/*
 * pke_clock.c: show the PKE clock and check it at two speeds
 *
 * Prints the PLL settings and frequency sensors, runs pke_clock_init, then
 * runs modular multiply and exponent tests at PKE dividers 0xFF and 0x7F.
 *
 * Wiring:
 *   PB14 --> USB serial RX
 */

#include "bao.h"
#include "hardware/pke.h"
#include "bigint_vectors.h"

static uint32_t buf[64];

static int same(const uint32_t *a, const uint32_t *b, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void clear(uint32_t *b, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        b[i] = 0;
}

static void show_clocks(const char *tag)
{
    pke_clock_status_t s;

    pke_clock_status(&s);
    mini_printf("  %s\r\n", tag);
    mini_printf("    PLLMN %08x  PLLQ %08x  CGUSEL0 %08x  CGUSEC %08x\r\n",
                s.pllmn, s.pllq, s.cgusel0, s.cgusec);
    mini_printf("    FDPKE %02x  FSVLD %02x  fclk %u  pke %u  pll0 %u  pll1 %u MHz\r\n",
                s.fdpke, s.fsvld, s.fclk_mhz, s.pke_mhz, s.pll0_mhz, s.pll1_mhz);
}

static void result(const char *name, int rc, int ok)
{
    mini_printf("    %s ", name);
    if (rc != 0)
        mini_printf("FAIL (status %d)\r\n", rc);
    else if (!ok)
        mini_printf("FAIL (wrong answer)\r\n");
    else
        mini_printf("PASS\r\n");
}

int main(void)
{
    static const uint32_t dividers[2] = { 0xFF, 0x7F };
    static const uint32_t e65537[1] = { 65537 };
    int rc;

    bao_init();

    mini_printf("\r\nPKE Clock Test\r\n\r\n");
    show_clocks("as boot1 left it");

    pke_clock_init();
    show_clocks("after pke_clock_init");

    for (uint32_t d = 0; d < 2; d++) {
        mini_printf("\r\nDivider 0x%02x\r\n", dividers[d]);
        pke_set_clock_divider(dividers[d]);
        delay_ms(10);
        show_clocks("after setting it");

        for (int i = 0; i < 3; i++) {
            clear(buf, 64);
            rc = pke_mod_mul(MOD_X, 32, MOD_Y, 32, RSA_N, 32, buf);
            result("x * y mod n  ", rc, same(buf, EXP_MUL, 32));
        }

        clear(buf, 64);
        rc = pke_mod_exp(RSA_M, 32, e65537, 1, RSA_N, 32, buf);
        result("m ^ e mod n  ", rc, same(buf, RSA_C, 32));
    }

    mini_printf("\r\n[Done]\r\n");
    while (1) {
        __asm__ volatile ("wfi");
    }
}
