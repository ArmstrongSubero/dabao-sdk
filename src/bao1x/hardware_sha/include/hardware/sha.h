/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware SHA-256 hash driver for the Baochip-1x SCE.
 *
 * Uses the on-chip hash engine to compute SHA-256 digests.
 * Currently supports messages up to 55 bytes (single block).
 */

#ifndef _HARDWARE_SHA_H
#define _HARDWARE_SHA_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_SIZE  32

/*
 * Initialize the SHA-256 hash engine.
 * Loads the H and K constants into the SCE. Must be called once
 * before any sha256_hash calls.
 */
void sha256_init(void);

/*
 * Compute a SHA-256 hash.
 * msg:     input message
 * msg_len: length in bytes (max 55 for single-block)
 * digest:  output buffer (32 bytes)
 *
 * Returns 0 on success, -1 if msg_len > 55.
 */
int sha256_hash(const uint8_t *msg, uint32_t msg_len, uint8_t *digest);

#ifdef __cplusplus
}
#endif

#endif
