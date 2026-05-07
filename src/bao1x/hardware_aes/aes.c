/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware AES driver for the Baochip-1x SCE.
 */

#include "hardware/aes.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sce.h"
#include "sevs_runtime.h"

/** @brief Maximum poll iterations for AES completion. */
#define AES_POLL_TIMEOUT 1000000

/* Key mode set during aes_set_key, used by encrypt/decrypt */
static uint8_t s_key_mode = 0;

/** @brief Load bytes into AES registers in big-endian word order. */
static void s_aes_load_bytes(const uint8_t *src, volatile uint32_t *dst, uint32_t byte_len)
{
    SEVS_ASSERT(byte_len % 4 == 0);
    SEVS_ASSERT(byte_len > 0);
    for (uint32_t i = 0; i < byte_len / 4; i++) {
        dst[i] = ((uint32_t)src[i*4+0] << 24) | ((uint32_t)src[i*4+1] << 16)
               | ((uint32_t)src[i*4+2] << 8)  | ((uint32_t)src[i*4+3]);
    }
}

/** @brief Read AES output registers into a byte buffer. */
static void s_aes_read_bytes(volatile uint32_t *src, uint8_t *dst, uint32_t word_count)
{
    SEVS_ASSERT(word_count > 0);
    SEVS_ASSERT(word_count <= 64);
    for (uint32_t i = 0; i < word_count; i++) {
        uint32_t v = src[i];
        dst[i*4+0] = (v >> 24) & 0xFF;
        dst[i*4+1] = (v >> 16) & 0xFF;
        dst[i*4+2] = (v >> 8) & 0xFF;
        dst[i*4+3] = v & 0xFF;
    }
}

/** @brief Trigger the AES engine. */
static void s_aes_trigger(void)
{
    SEVS_ASSERT(AES_POLL_TIMEOUT > 0);
    AES_FR = 0x0F;
    memory_fence();
    AES_AR = 0x5A;
    memory_fence();
}

/** @brief Wait for the AES engine to complete. */
static void s_aes_wait_done(void)
{
    SEVS_ASSERT(AES_POLL_TIMEOUT > 0);
    for (int s_poll = 0; s_poll < AES_POLL_TIMEOUT && !(AES_FR & 0x01); s_poll++) {
        /* bounded poll */
    }
    AES_FR |= 0x01;
    memory_fence();
}

/** @brief Load an AES key and run the key schedule.
 *  @param[in] key     Pointer to the key bytes.
 *  @param[in] key_len Key length: AES_KEY_128, AES_KEY_192, or AES_KEY_256.
 *  @return 0 on success, -1 on invalid key length.
 *  @req REQ-DABAO-AES-0001 */
int aes_set_key(const uint8_t *key, uint32_t key_len)
{
    SEVS_REQUIRE_NOT_NULL(key);

    switch (key_len) {
        case AES_KEY_128: s_key_mode = 0x00; break;
        case AES_KEY_192: s_key_mode = 0x01; break;
        case AES_KEY_256: s_key_mode = 0x02; break;
        default: return -1;
    }

    /* Enable SCE AES + SDMA clocks */
    SCE_SCEMODE = 0;
    SCE_SUBCLKEN |= (1 << 1) | (1 << 0);
    memory_fence();

    /* Load key and run key schedule */
    AES_OPT3 = 0;
    s_aes_load_bytes(key, AES_KEY_BUF, key_len);

    AES_OPT1 = s_key_mode;
    AES_CRFUNC = AES_FUNC_KS;
    s_aes_trigger();
    s_aes_wait_done();

    return 0;
}

/** @brief ECB encrypt or decrypt a data block.  */
static int s_aes_ecb_crypt(const uint8_t *in, uint8_t *out, uint32_t len, uint8_t func)
{
    SEVS_ASSERT(AES_POLL_TIMEOUT > 0);
    if (len % 16 != 0 || len == 0) {
        return -1;
    }

    AES_OPT3 = 0;
    s_aes_load_bytes(in, AES_IN_BUF, len);

    AES_OPT1 = s_key_mode;
    AES_OPT2 = (len / 16) - 1;
    AES_CRFUNC = func;

    s_aes_trigger();
    s_aes_wait_done();

    s_aes_read_bytes(AES_OUT_BUF, out, len / 4);

    return 0;
}

/** @brief Encrypt data using AES-ECB.
 *  @param[in]  in  Pointer to plaintext (must be 16-byte aligned length).
 *  @param[out] out Pointer to ciphertext output buffer.
 *  @param[in]  len Length in bytes (must be multiple of 16).
 *  @return 0 on success, -1 on invalid length.
 *  @req REQ-DABAO-AES-0002 */
int aes_encrypt(const uint8_t *in, uint8_t *out, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(in);
    SEVS_REQUIRE_NOT_NULL(out);
    return s_aes_ecb_crypt(in, out, len, AES_FUNC_ENC);
}

/** @brief Decrypt data using AES-ECB.
 *  @param[in]  in  Pointer to ciphertext.
 *  @param[out] out Pointer to plaintext output buffer.
 *  @param[in]  len Length in bytes (must be multiple of 16).
 *  @return 0 on success, -1 on invalid length.
 *  @req REQ-DABAO-AES-0003 */
int aes_decrypt(const uint8_t *in, uint8_t *out, uint32_t len)
{
    SEVS_REQUIRE_NOT_NULL(in);
    SEVS_REQUIRE_NOT_NULL(out);
    return s_aes_ecb_crypt(in, out, len, AES_FUNC_DEC);
}
