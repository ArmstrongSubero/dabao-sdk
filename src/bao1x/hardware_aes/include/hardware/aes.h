/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware AES encryption driver for the Baochip-1x SCE.
 *
 * Supports AES-128, AES-192, and AES-256 in ECB mode using the
 * on-chip Secure Computing Engine. Processes 16-byte blocks.
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

#ifdef __cplusplus
}
#endif

#endif
