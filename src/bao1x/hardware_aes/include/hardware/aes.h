/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware AES encryption driver for the Baochip-1x SCE.
 *
 * Supports AES-128, AES-192, and AES-256 in ECB, CBC, CTR, OFB and CFB
 * (128 bit feedback) modes using the on-chip Secure Computing Engine.
 * Processes 16-byte blocks.
 */

#ifndef _HARDWARE_AES_H
#define _HARDWARE_AES_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Key sizes */
#define AES_KEY_128     16
#define AES_KEY_192     24
#define AES_KEY_256     32

/* Block cipher modes, the opt_mode field of the AES OPT register */
#define AES_MODE_ECB    0
#define AES_MODE_CBC    1
#define AES_MODE_CTR    2
#define AES_MODE_OFB    3
#define AES_MODE_CFB    4

/*
 * Set the AES key. Enables the SCE clock and runs the key schedule.
 * key:     pointer to the key bytes
 * key_len: AES_KEY_128, AES_KEY_192, or AES_KEY_256
 *
 * Returns 0 on success, -1 on invalid key length.
 */
int aes_set_key(const uint8_t *key, uint32_t key_len);

/*
 * Encrypt data in ECB mode.
 * in:  plaintext (must be a multiple of 16 bytes)
 * out: ciphertext output buffer (same size as in)
 * len: number of bytes (must be a multiple of 16)
 *
 * Returns 0 on success, -1 on invalid length.
 */
int aes_encrypt(const uint8_t *in, uint8_t *out, uint32_t len);

/*
 * Decrypt data in ECB mode.
 * in:  ciphertext (must be a multiple of 16 bytes)
 * out: plaintext output buffer (same size as in)
 * len: number of bytes (must be a multiple of 16)
 *
 * Returns 0 on success, -1 on invalid length.
 */
int aes_decrypt(const uint8_t *in, uint8_t *out, uint32_t len);

/*
 * Set the 16 byte IV for CBC, OFB and CFB, or the initial counter block
 * for CTR. Call it after aes_set_key, since the engine reads the IV from
 * the start of the key buffer. After each block the engine writes the
 * next chaining value back there, so consecutive calls of
 * aes_encrypt_mode or aes_decrypt_mode continue one message. In CTR mode
 * the last 32 bits of the counter block count up as a big endian number
 * and wrap without carrying into the bytes before them.
 *
 * Returns 0.
 */
int aes_set_iv(const uint8_t *iv);

/*
 * Encrypt or decrypt data in the given mode (AES_MODE_ECB to AES_MODE_CFB).
 * in:  input (must be a multiple of 16 bytes)
 * out: output buffer (same size as in)
 * len: number of bytes (must be a multiple of 16)
 *
 * Returns 0 on success, -1 on invalid length or mode.
 */
int aes_encrypt_mode(uint32_t mode, const uint8_t *in, uint8_t *out, uint32_t len);
int aes_decrypt_mode(uint32_t mode, const uint8_t *in, uint8_t *out, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif
