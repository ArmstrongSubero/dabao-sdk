/*
 * sha.c - SHA-256 hardware hash using the SHA driver
 *
 * Hashes the empty string and "abc" using the hardware SHA-256 engine.
 * Verifies against known NIST test vectors.
 *
 * Wiring:
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/sha.h"

static void print_hex(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        mini_printf("%02x", data[i]);
}

int main(void)
{
    bao_init();

    mini_printf("\r\nSHA-256 Hardware Hash Demo\r\n\r\n");

    sha256_init();

    uint8_t digest[SHA256_DIGEST_SIZE];

    /* Test 1: empty string */
    mini_printf("[Test 1] SHA-256(\"\")\r\n");
    sha256_hash((const uint8_t *)"", 0, digest);
    mini_printf("  Got:    "); print_hex(digest, 32); mini_printf("\r\n");
    mini_printf("  Expect: e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855\r\n\r\n");

    /* Test 2: "abc" */
    mini_printf("[Test 2] SHA-256(\"abc\")\r\n");
    sha256_init();
    sha256_hash((const uint8_t *)"abc", 3, digest);
    mini_printf("  Got:    "); print_hex(digest, 32); mini_printf("\r\n");
    mini_printf("  Expect: ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\r\n\r\n");

    /* Test 3: longer message */
    mini_printf("[Test 3] SHA-256(\"Hello from Dabao!\")\r\n");
    sha256_init();
    sha256_hash((const uint8_t *)"Hello from Dabao!", 17, digest);
    mini_printf("  Got:    "); print_hex(digest, 32); mini_printf("\r\n");

    mini_printf("\r\n[Done] SHA-256 demo complete.\r\n");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
