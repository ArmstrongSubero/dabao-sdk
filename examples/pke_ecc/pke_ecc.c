/*
 * pke_ecc.c: elliptic curve operations on the PKE
 *
 * Checks P-256, secp256k1, P-384, X25519 (RFC 7748) and Ed25519 point
 * operations against known answers, printing PASS or FAIL with the time
 * taken.
 *
 * Wiring:
 *   PB14 --> USB serial RX
 */

#include "bao.h"
#include "hardware/pke.h"
#include "ecc_vectors.h"

static uint32_t pt[24];
static int failures;

static int same32(const uint32_t *a, const uint32_t *b, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static int same8(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
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

int main(void)
{
    uint64_t t0;
    int rc;

    bao_init();

    mini_printf("\r\nPKE Elliptic Curve Demo\r\n\r\n");
    mini_printf("P-256:\r\n");

    t0 = millis();
    rc = pke_ecc_point_mul_base(PKE_SECP256R1, P256_KA, pt);
    report("ka * G", rc, same32(pt, P256_A, 16), t0);

    t0 = millis();
    rc = pke_ecc_point_mul_base(PKE_SECP256R1, P256_KB, pt);
    report("kb * G", rc, same32(pt, P256_B, 16), t0);

    t0 = millis();
    rc = pke_ecc_point_add(PKE_SECP256R1, P256_A, P256_B, pt);
    report("A + B", rc, same32(pt, P256_A_PLUS_B, 16), t0);

    t0 = millis();
    rc = pke_ecc_point_double(PKE_SECP256R1, P256_A, pt);
    report("2 * A", rc, same32(pt, P256_2A, 16), t0);

    mini_printf("\r\nOther curves:\r\n");

    t0 = millis();
    rc = pke_ecc_point_mul_base(PKE_SECP256K1, K1_K, pt);
    report("secp256k1 k * G", rc, same32(pt, K1_KG, 16), t0);

    t0 = millis();
    rc = pke_ecc_point_mul_base(PKE_SECP384R1, P384_K, pt);
    report("P-384 k * G", rc, same32(pt, P384_KG, 24), t0);

    mini_printf("\r\nX25519 (RFC 7748):\r\n");

    uint8_t key[32];

    t0 = millis();
    rc = pke_x25519_public(X_ALICE_PRIV, key);
    report("Alice public key", rc, same8(key, X_ALICE_PUB, 32), t0);

    t0 = millis();
    rc = pke_x25519_public(X_BOB_PRIV, key);
    report("Bob public key", rc, same8(key, X_BOB_PUB, 32), t0);

    t0 = millis();
    rc = pke_x25519_shared(X_ALICE_PRIV, X_BOB_PUB, key);
    report("shared, Alice side", rc, same8(key, X_SHARED, 32), t0);

    t0 = millis();
    rc = pke_x25519_shared(X_BOB_PRIV, X_ALICE_PUB, key);
    report("shared, Bob side", rc, same8(key, X_SHARED, 32), t0);

    mini_printf("\r\nEd25519 points:\r\n");

    pke_ed_point_t kb, r;

    t0 = millis();
    rc = pke_ed25519_point_mul_base(ED_K, &kb);
    report("k * B", rc, same8(kb.x, ED_KB_X, 32) && same8(kb.y, ED_KB_Y, 32), t0);

    t0 = millis();
    rc = pke_ed25519_point_double(&kb, &r);
    report("2 * kB", rc, same8(r.x, ED_2KB_X, 32) && same8(r.y, ED_2KB_Y, 32), t0);

    pke_ed_point_t b;
    static const uint8_t one[32] = { 1 };
    rc = pke_ed25519_point_mul_base(one, &b);
    t0 = millis();
    if (rc == 0)
        rc = pke_ed25519_point_add(&kb, &b, &r);
    report("kB + B", rc, same8(r.x, ED_KB1_X, 32) && same8(r.y, ED_KB1_Y, 32), t0);

    if (failures == 0)
        mini_printf("\r\n[Done] all tests passed.\r\n");
    else
        mini_printf("\r\n[Done] %d test(s) failed.\r\n", failures);

    while (1) {
        __asm__ volatile ("wfi");
    }
}
