/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Hardware SHA-256 driver for the Baochip-1x SCE.
 * Extracted from verified main_sha.c bare metal example.
 */

#include "hardware/sha.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sce.h"
#include "sevs_runtime.h"

/** @brief Maximum poll iterations for hash completion. */
#define HASH_POLL_TIMEOUT 1000000

/** @brief Maximum single-block message length (55 bytes for SHA-256 padding). */
#define SHA256_MAX_MSG_LEN 55

static const uint32_t s_sha256_h[8] = {
    0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
    0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
};

static const uint32_t s_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

/** @brief Load uint32 array into hash registers. */
static void s_hash_load_u32(const uint32_t *src, volatile uint32_t *dst, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        dst[i] = src[i];
    }
}

/** @brief Load bytes into hash registers in big-endian word order. */
static void s_hash_load_bytes(const uint8_t *src, volatile uint32_t *dst, uint32_t len)
{
    for (uint32_t i = 0; i < len / 4; i++) {
        dst[i] = ((uint32_t)src[i*4+0] << 24) | ((uint32_t)src[i*4+1] << 16)
               | ((uint32_t)src[i*4+2] << 8)  | ((uint32_t)src[i*4+3]);
    }
}

/** @brief Read hash output registers into a byte buffer. */
static void s_hash_read_bytes(volatile uint32_t *src, uint8_t *dst, uint32_t word_count)
{
    for (uint32_t i = 0; i < word_count; i++) {
        uint32_t v = src[i];
        dst[i*4+0] = (v >> 24) & 0xFF;
        dst[i*4+1] = (v >> 16) & 0xFF;
        dst[i*4+2] = (v >> 8) & 0xFF;
        dst[i*4+3] = v & 0xFF;
    }
}

/** @brief Trigger the hash engine. */
static void s_hash_trigger(void)
{
    HASH_FR = 0x0F;
    memory_fence();
    HASH_AR = 0x5A;
    memory_fence();
}

/** @brief Wait for the hash engine to complete. */
static void s_hash_wait_done(void)
{
    for (int s_poll = 0; s_poll < HASH_POLL_TIMEOUT && !(HASH_FR & 0x01); s_poll++) {
        /* bounded poll */
    }
    HASH_FR |= 0x01;
    memory_fence();
}

/** @brief Initialize the SHA-256 engine with initial hash values and constants.
 *  @req REQ-DABAO-SHA-0001 */
void sha256_init(void)
{
    SEVS_ASSERT(sizeof(s_sha256_h) == 32);
    SEVS_ASSERT(sizeof(s_sha256_k) == 256);
    SCE_SCEMODE = 0;
    SCE_SUBCLKEN |= (1 << 2);
    memory_fence();

    s_hash_load_u32(s_sha256_h, HASH_LKEY_BUF, 8);
    s_hash_load_u32(s_sha256_k, HASH_LKEY_BUF + 8, 64);

    HASH_OPT3 = 0;
    HASH_CRFUNC = HASH_FUNC_INIT;
    s_hash_trigger();
    s_hash_wait_done();
}

/** @brief Compute SHA-256 hash of a single-block message.
 *  @param[in]  msg     Input message (max 55 bytes).
 *  @param[in]  msg_len Length of the message in bytes.
 *  @param[out] digest  32-byte output buffer for the hash.
 *  @return 0 on success, -1 if message too long.
 *  @req REQ-DABAO-SHA-0002 */
int sha256_hash(const uint8_t *msg, uint32_t msg_len, uint8_t *digest)
{
    SEVS_REQUIRE_NOT_NULL(msg);
    SEVS_REQUIRE_NOT_NULL(digest);
    SEVS_ASSERT(msg_len <= SHA256_MAX_MSG_LEN);
    uint8_t padded[64];

    if (msg_len > SHA256_MAX_MSG_LEN) {
        return -1;
    }

    /* Build padded block: message + 0x80 + zeros + 64-bit bit length */
    for (uint32_t i = 0; i < msg_len; i++) {
        padded[i] = msg[i];
    }
    padded[msg_len] = 0x80;
    for (uint32_t i = msg_len + 1; i < 56; i++) {
        padded[i] = 0;
    }

    uint64_t bit_len = (uint64_t)msg_len * 8;
    padded[56] = (bit_len >> 56) & 0xFF;
    padded[57] = (bit_len >> 48) & 0xFF;
    padded[58] = (bit_len >> 40) & 0xFF;
    padded[59] = (bit_len >> 32) & 0xFF;
    padded[60] = (bit_len >> 24) & 0xFF;
    padded[61] = (bit_len >> 16) & 0xFF;
    padded[62] = (bit_len >> 8)  & 0xFF;
    padded[63] = (bit_len >> 0)  & 0xFF;

    s_hash_load_bytes(padded, HASH_MSG_BUF, 64);

    HASH_OPT1 = 0;
    HASH_OPT2 = (1 << 2);
    HASH_OPT3 = 0;
    HASH_MSG_REG = 0;
    HASH_HOUT_REG = 0;
    HASH_CRFUNC = HASH_FUNC_SHA256;

    s_hash_trigger();
    s_hash_wait_done();

    s_hash_read_bytes(HASH_OUT_BUF, digest, 8);
    return 0;
}
