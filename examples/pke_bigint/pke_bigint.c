/*
 * pke_bigint.c: big integer and modular arithmetic on the PKE and ALU
 *
 * Checks 1024 bit modular add, subtract, multiply and exponentiation,
 * 256 bit inverse, 512 bit gcd and ALU divide, add and subtract against
 * known answers, printing PASS or FAIL with the time taken.
 *
 * Wiring:
 *   PB14 --> USB serial RX
 */

#include "bao.h"
#include "hardware/pke.h"
#include "hardware/alu.h"
#include "bigint_vectors.h"

static uint32_t buf[64];
static uint32_t buf2[64];
static int failures;

static int same(const uint32_t *a, const uint32_t *b, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void report(const char *name, int rc, int ok, uint64_t t0)
{
    uint32_t ms = (uint32_t)(millis() - t0);
    uint32_t n = 0;

    mini_printf("  %s", name);
    while (name[n] != '\0')
        n++;
    while (n++ < 24)
        mini_printf(" ");

    if (rc != 0) {
        mini_printf("FAIL (status %d)\r\n", rc);
        failures++;
    } else if (!ok) {
        mini_printf("FAIL (wrong answer)\r\n");
        failures++;
    } else {
        mini_printf("PASS  %u ms\r\n", ms);
    }
}

static void clear(uint32_t *b, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        b[i] = 0;
}

int main(void)
{
    uint64_t t0;
    int rc;

    bao_init();

    mini_printf("\r\nPKE and ALU Big Integer Demo\r\n\r\n");
    mini_printf("Modular arithmetic, 1024 bit modulus:\r\n");

    clear(buf, 64); t0 = millis();
    rc = pke_mod_add(MOD_X, 32, MOD_Y, 32, RSA_N, 32, buf);
    report("x + y mod n", rc, same(buf, EXP_ADD, 32), t0);

    clear(buf, 64); t0 = millis();
    rc = pke_mod_sub(MOD_X, 32, MOD_Y, 32, RSA_N, 32, buf);
    report("x minus y mod n", rc, same(buf, EXP_SUB, 32), t0);

    clear(buf, 64); t0 = millis();
    rc = pke_mod_mul(MOD_X, 32, MOD_Y, 32, RSA_N, 32, buf);
    report("x * y mod n", rc, same(buf, EXP_MUL, 32), t0);

    static const uint32_t e65537[1] = { 65537 };
    clear(buf, 64); t0 = millis();
    rc = pke_mod_exp(RSA_M, 32, e65537, 1, RSA_N, 32, buf);
    report("m ^ e mod n", rc, same(buf, RSA_C, 32), t0);

    clear(buf2, 64); t0 = millis();
    rc = pke_mod_exp(RSA_C, 32, RSA_D, 32, RSA_N, 32, buf2);
    report("c ^ d mod n", rc, same(buf2, RSA_M, 32), t0);

    mini_printf("\r\nInverse and gcd:\r\n");

    clear(buf, 64); t0 = millis();
    rc = pke_mod_inv(INV_A, 8, INV_P, 8, buf);
    report("1 / a mod p (256 bit)", rc, same(buf, EXP_INV, 8), t0);

    clear(buf, 64); t0 = millis();
    rc = pke_gcd(GCD_X, 16, GCD_Y, 16, buf);
    report("gcd (512 bit)", rc, same(buf, EXP_GCD, 16), t0);

    mini_printf("\r\nALU:\r\n");

    uint32_t qw = 0, rw = 0;
    clear(buf, 64); clear(buf2, 64); t0 = millis();
    rc = alu_div(DIV_X, 32, DIV_Y, 10, buf, &qw, buf2, &rw);
    report("1024 / 300 bit", rc,
           same(buf, EXP_Q, 32) && same(buf2, EXP_R, 10), t0);

    uint32_t carry = 0;
    clear(buf, 64); t0 = millis();
    rc = alu_add(ADD_X, 16, ADD_Y, 16, buf, &carry);
    report("512 bit add", rc, same(buf, EXP_SUM, 16) && carry == 0, t0);

    clear(buf, 64); t0 = millis();
    rc = alu_sub(ADD_X, 16, ADD_Y, 16, buf, &carry);
    report("512 bit subtract", rc, same(buf, EXP_DIFF, 16) && carry == 0, t0);

    if (failures == 0)
        mini_printf("\r\n[Done] all tests passed.\r\n");
    else
        mini_printf("\r\n[Done] %d test(s) failed.\r\n", failures);

    while (1) {
        __asm__ volatile ("wfi");
    }
}
