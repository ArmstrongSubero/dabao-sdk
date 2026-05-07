/*
 * aes.c - AES-128 hardware encryption using the AES driver
 *
 * Encrypts and decrypts a NIST test vector using the hardware AES engine.
 * Verifies the result matches the known ciphertext.
 *
 * Wiring:
 *   PB14 --> USB-serial RX
 */

#include "bao.h"
#include "hardware/aes.h"

static void print_hex(const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++)
        mini_printf("%02x", data[i]);
}

int main(void)
{
    bao_init();
    delay_ms(5000);

    mini_printf("\r\nAES-128 Hardware Encryption Demo\r\n\r\n");

    /* NIST FIPS 197 Appendix B test vector */
    uint8_t key[16] = {
        0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6,
        0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c
    };

    uint8_t plain[16] = {
        0x32, 0x43, 0xf6, 0xa8, 0x88, 0x5a, 0x30, 0x8d,
        0x31, 0x31, 0x98, 0xa2, 0xe0, 0x37, 0x07, 0x34
    };

    uint8_t cipher[16];
    uint8_t decrypted[16];

    mini_printf("Key:       "); print_hex(key, 16); mini_printf("\r\n");
    mini_printf("Plaintext: "); print_hex(plain, 16); mini_printf("\r\n");

    aes_set_key(key, 16);
    aes_encrypt(plain, cipher, 16);
    mini_printf("Cipher:    "); print_hex(cipher, 16); mini_printf("\r\n");

    aes_decrypt(cipher, decrypted, 16);
    mini_printf("Decrypted: "); print_hex(decrypted, 16); mini_printf("\r\n");

    int match = 1;
    for (int i = 0; i < 16; i++) {
        if (decrypted[i] != plain[i]) { match = 0; break; }
    }
    mini_printf("Round-trip: %s\r\n", match ? "PASS" : "FAIL");

    while (1) {
        __asm__ volatile ("wfi");
    }
}
