/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware SHA-256 hash driver for the Baochip-1x SCE.
 *
 * Uses the on-chip hash engine to compute SHA-256 digests.
 * sha256_hash supports messages up to 55 bytes (single block).
 *
 * The streaming calls (sha_stream.c) hash any length with SHA-256 or
 * SHA-512 through a context the caller owns, and compute HMAC over either
 * with the engine's own HMAC functions. Contexts are independent: any
 * number may be used in turn. They need no sha256_init; every call that
 * hashes loads the engine's constants itself.
 */

#ifndef _HARDWARE_SHA_H
#define _HARDWARE_SHA_H

#include "bao/platform.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SHA256_DIGEST_SIZE  32
#define SHA256_BLOCK_SIZE   64
#define SHA512_DIGEST_SIZE  64
#define SHA512_BLOCK_SIZE   128

/* Return codes of the streaming and HMAC calls */
#define SHA_OK              0
#define SHA_ERR_ARG         (-1)    /* NULL pointer with a nonzero length */
#define SHA_ERR_STATE       (-2)    /* context not initialised, or already final */
#define SHA_ERR_TIMEOUT     (-3)    /* the engine did not finish */

/* State shared by every context; treat as opaque. state holds the running
 * hash between engine starts (8 words for SHA-256, 16 for SHA-512), buf
 * the bytes of a block not yet complete. */
typedef struct {
    uint32_t state[16];
    uint8_t  buf[SHA512_BLOCK_SIZE];
    uint64_t total;
    uint32_t buf_len;
    uint32_t flags;
} sha2_stream_t;

typedef struct { sha2_stream_t s; } sha256_ctx_t;
typedef struct { sha2_stream_t s; } sha512_ctx_t;
/* key holds the HMAC key block (the key, or its hash, padded with zeros) */
typedef struct { sha2_stream_t s; uint32_t key[SHA256_BLOCK_SIZE / 4]; } hmac_sha256_ctx_t;
typedef struct { sha2_stream_t s; uint32_t key[SHA512_BLOCK_SIZE / 4]; } hmac_sha512_ctx_t;

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

/*
 * Streaming SHA-256 and SHA-512. init, then update any number of times
 * with any length (0 included), then final, which writes the digest and
 * clears the context. Each returns SHA_OK or a SHA_ERR code; after an
 * error from update or final the context is cleared and must be
 * initialised again.
 */
int sha256_stream_init(sha256_ctx_t *ctx);
int sha256_stream_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);
int sha256_stream_final(sha256_ctx_t *ctx, uint8_t *digest);

int sha512_stream_init(sha512_ctx_t *ctx);
int sha512_stream_update(sha512_ctx_t *ctx, const uint8_t *data, uint32_t len);
int sha512_stream_final(sha512_ctx_t *ctx, uint8_t *digest);

/* SHA-512 of a whole message in one call; digest is 64 bytes. */
int sha512_hash(const uint8_t *msg, uint32_t msg_len, uint8_t *digest);

/*
 * HMAC-SHA256 and HMAC-SHA512 (RFC 2104) with keys of any length. The one
 * call form, or init with the key, update, final. mac is 32 or 64 bytes;
 * final clears the context and the key.
 */
int hmac_sha256(const uint8_t *key, uint32_t key_len, const uint8_t *msg, uint32_t msg_len,
                uint8_t *mac);
int hmac_sha256_init(hmac_sha256_ctx_t *ctx, const uint8_t *key, uint32_t key_len);
int hmac_sha256_update(hmac_sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);
int hmac_sha256_final(hmac_sha256_ctx_t *ctx, uint8_t *mac);

int hmac_sha512(const uint8_t *key, uint32_t key_len, const uint8_t *msg, uint32_t msg_len,
                uint8_t *mac);
int hmac_sha512_init(hmac_sha512_ctx_t *ctx, const uint8_t *key, uint32_t key_len);
int hmac_sha512_update(hmac_sha512_ctx_t *ctx, const uint8_t *data, uint32_t len);
int hmac_sha512_final(hmac_sha512_ctx_t *ctx, uint8_t *mac);

#ifdef __cplusplus
}
#endif

#endif
