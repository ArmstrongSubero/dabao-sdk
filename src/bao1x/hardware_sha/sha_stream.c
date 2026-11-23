/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Streaming SHA-256 and SHA-512, and HMAC over both, on the Baochip-1x SCE
 * hash engine.
 *
 * How the engine keeps its state (combohasha.sv and hashcore.sv in the
 * Baochip-1x RTL):
 *
 *  - The engine has its own RAM that the CPU cannot reach. HASH_FUNC_INIT
 *    copies SCE RAM words 0 to 427 into it, and the cores take their H
 *    and K constants from there. This driver writes the SHA-256 and the
 *    SHA-512 constants (words 0 to 247) and runs INIT before every call
 *    that hashes, so whatever ran on the engine in between (sha256_init,
 *    which loads only the SHA-256 words, or anything else) cannot leave it
 *    with the wrong constants.
 *  - Between two starts the running state lives in the HOUT segment: a
 *    start without HASH_OPT2_IFSTART loads it from HOUT (LD_ST), and every
 *    start writes the new state back to HOUT. While SCEMODE is 0 the
 *    segment access rules are off (sce.sv 370), so the CPU can write HOUT.
 *    Each context keeps its own copy of the state and puts it back into
 *    HOUT before it continues, so any number of contexts can be used in
 *    turn.
 *  - The engine never pads a message. HMAC pass 1 hashes the key block
 *    XOR 0x36 and then whole message blocks, so the driver pads the last
 *    block itself with the length of the key block plus the message. Pass
 *    2 hashes the key block XOR 0x5C and the inner hash with the padding
 *    the engine adds itself (combohasha.sv 874 to 879). A key longer than
 *    one block is hashed first with the key hash function, which leaves
 *    the hash in the KEY segment. Both functions take any number of
 *    blocks and continue across starts, so the engine's own HMAC covers
 *    every key and message length.
 *
 * Register sequences cross checked against the CrossBar Daric SDK
 * (drivers/Src/sce/hash.c, Apache 2.0). No dynamic memory.
 */

#include "hardware/sha.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sce.h"
#include "sevs_runtime.h"

/** @brief Maximum poll iterations for one engine start. */
#define SHA2_POLL_TIMEOUT 1000000

/** @brief Context flags: a magic value, the algorithm and two state bits. */
#define SHA2_MAGIC        0x5A2C0000u
#define SHA2_MAGIC_MASK   0xFFFF0000u
#define SHA2_ALG_SHIFT    8
#define SHA2_STARTED      (1u << 0)   /* the engine holds a state for it */
#define SHA2_HMAC         (1u << 1)   /* blocks go through HMAC pass 1 */

/** @brief The words of SCE RAM HASH_FUNC_INIT reads that hold SHA-2. */
#define SHA2_CONST_WORDS  248

/** @brief What differs between SHA-256 and SHA-512 on the engine. */
typedef struct {
    uint32_t block;         /* block size in bytes */
    uint32_t msg_words;     /* words of one block (MSGSIZE) */
    uint32_t st_words;      /* words of the state (STSIZE) */
    uint32_t msg_cap;       /* blocks the MSG segment holds (128 words) */
    uint32_t lkey_cap;      /* blocks the LKEY segment holds (64 words) */
    uint32_t len_bytes;     /* bytes of the length field in the padding */
    uint32_t fn_hash;
    uint32_t fn_keyhash;
    uint32_t fn_pass1;
    uint32_t fn_pass2;
} s_alg_t;

enum { ALG_SHA256 = 1, ALG_SHA512 = 2 };

static const s_alg_t s_algs[3] = {
    {0},
    {64, 16, 8, 8, 4, 8, HASH_FUNC_SHA256, HASH_FUNC_HMAC256_KEYHASH,
     HASH_FUNC_HMAC256_PASS1, HASH_FUNC_HMAC256_PASS2},
    {128, 32, 16, 4, 2, 16, HASH_FUNC_SHA512, HASH_FUNC_HMAC512_KEYHASH,
     HASH_FUNC_HMAC512_PASS1, HASH_FUNC_HMAC512_PASS2},
};

/*
 * The constants in the order HASH_FUNC_INIT puts them in the engine RAM
 * (hash_pkg.sv: SHA-256 H at 0, K at 8, SHA-512 H at 72, K at 88). A 64
 * bit constant is two words, the high half first. FIPS 180-4 sections
 * 4.2.2, 4.2.3, 5.3.3 and 5.3.5.
 */
static const uint32_t s_sha2_const[SHA2_CONST_WORDS] = {
    /* SHA-256 H */
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    /* SHA-256 K */
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
    /* SHA-512 H */
    0x6a09e667, 0xf3bcc908, 0xbb67ae85, 0x84caa73b,
    0x3c6ef372, 0xfe94f82b, 0xa54ff53a, 0x5f1d36f1,
    0x510e527f, 0xade682d1, 0x9b05688c, 0x2b3e6c1f,
    0x1f83d9ab, 0xfb41bd6b, 0x5be0cd19, 0x137e2179,
    /* SHA-512 K */
    0x428a2f98, 0xd728ae22, 0x71374491, 0x23ef65cd,
    0xb5c0fbcf, 0xec4d3b2f, 0xe9b5dba5, 0x8189dbbc,
    0x3956c25b, 0xf348b538, 0x59f111f1, 0xb605d019,
    0x923f82a4, 0xaf194f9b, 0xab1c5ed5, 0xda6d8118,
    0xd807aa98, 0xa3030242, 0x12835b01, 0x45706fbe,
    0x243185be, 0x4ee4b28c, 0x550c7dc3, 0xd5ffb4e2,
    0x72be5d74, 0xf27b896f, 0x80deb1fe, 0x3b1696b1,
    0x9bdc06a7, 0x25c71235, 0xc19bf174, 0xcf692694,
    0xe49b69c1, 0x9ef14ad2, 0xefbe4786, 0x384f25e3,
    0x0fc19dc6, 0x8b8cd5b5, 0x240ca1cc, 0x77ac9c65,
    0x2de92c6f, 0x592b0275, 0x4a7484aa, 0x6ea6e483,
    0x5cb0a9dc, 0xbd41fbd4, 0x76f988da, 0x831153b5,
    0x983e5152, 0xee66dfab, 0xa831c66d, 0x2db43210,
    0xb00327c8, 0x98fb213f, 0xbf597fc7, 0xbeef0ee4,
    0xc6e00bf3, 0x3da88fc2, 0xd5a79147, 0x930aa725,
    0x06ca6351, 0xe003826f, 0x14292967, 0x0a0e6e70,
    0x27b70a85, 0x46d22ffc, 0x2e1b2138, 0x5c26c926,
    0x4d2c6dfc, 0x5ac42aed, 0x53380d13, 0x9d95b3df,
    0x650a7354, 0x8baf63de, 0x766a0abb, 0x3c77b2a8,
    0x81c2c92e, 0x47edaee6, 0x92722c85, 0x1482353b,
    0xa2bfe8a1, 0x4cf10364, 0xa81a664b, 0xbc423001,
    0xc24b8b70, 0xd0f89791, 0xc76c51a3, 0x0654be30,
    0xd192e819, 0xd6ef5218, 0xd6990624, 0x5565a910,
    0xf40e3585, 0x5771202a, 0x106aa070, 0x32bbd1b8,
    0x19a4c116, 0xb8d2d0c8, 0x1e376c08, 0x5141ab53,
    0x2748774c, 0xdf8eeb99, 0x34b0bcb5, 0xe19b48a8,
    0x391c0cb3, 0xc5c95a63, 0x4ed8aa4a, 0xe3418acb,
    0x5b9cca4f, 0x7763e373, 0x682e6ff3, 0xd6b2b8a3,
    0x748f82ee, 0x5defb2fc, 0x78a5636f, 0x43172f60,
    0x84c87814, 0xa1f0ab72, 0x8cc70208, 0x1a6439ec,
    0x90befffa, 0x23631e28, 0xa4506ceb, 0xde82bde9,
    0xbef9a3f7, 0xb2c67915, 0xc67178f2, 0xe372532b,
    0xca273ece, 0xea26619c, 0xd186b8c7, 0x21c0c207,
    0xeada7dd6, 0xcde0eb1e, 0xf57d4f7f, 0xee6ed178,
    0x06f067aa, 0x72176fba, 0x0a637dc5, 0xa2c898a6,
    0x113f9804, 0xbef90dae, 0x1b710b35, 0x131c471b,
    0x28db77f5, 0x23047d84, 0x32caab7b, 0x40c72493,
    0x3c9ebe0a, 0x15c9bebc, 0x431d67c4, 0x9c100d4c,
    0x4cc5d4be, 0xcb3e42b6, 0x597f299c, 0xfc657e2a,
    0x5fcb6fab, 0x3ad6faec, 0x6c44198c, 0x4a475817,
};

/** @brief Clock the hash engine and leave SCEMODE at 0, as sha256_init does. */
static void s_engine_on(void)
{
    SCE_SCEMODE = 0;
    SCE_SUBCLKEN |= SCE_CLK_HASH;
    memory_fence();
}

/**
 * @brief Start one engine function with every segment pointer at 0 and no
 *        byte reversal, and wait for its done flag.
 * @return SHA_OK, or SHA_ERR_TIMEOUT if the engine did not finish.
 */
static int s_run(uint32_t func, uint32_t count, uint32_t opt2)
{
    int poll;

    HASH_OPT1 = count;
    HASH_OPT2 = opt2;
    HASH_OPT3 = 0;
    for (uint32_t i = 0; i < 8; i++) {
        HASH_SEGPTR(i) = 0;
    }
    HASH_CRFUNC = func;
    HASH_FR = 0x7F;
    memory_fence();
    HASH_AR = 0x5A;
    memory_fence();
    for (poll = 0; poll < SHA2_POLL_TIMEOUT && !(HASH_FR & 0x01); poll++) {
        /* bounded poll */
    }
    if (!(HASH_FR & 0x01)) {
        return SHA_ERR_TIMEOUT;
    }
    HASH_FR = 0x7F;
    memory_fence();
    return SHA_OK;
}

/** @brief Write the SHA-2 constants to SCE RAM and copy them in with INIT. */
static int s_load_constants(void)
{
    s_engine_on();
    for (uint32_t i = 0; i < SHA2_CONST_WORDS; i++) {
        HASH_LKEY_BUF[i] = s_sha2_const[i];
    }
    return s_run(HASH_FUNC_INIT, 0, 0);
}

/** @brief Store bytes as big endian words, the engine's word order. */
static void s_put_words(volatile uint32_t *dst, const uint8_t *src, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        dst[i] = ((uint32_t)src[i*4+0] << 24) | ((uint32_t)src[i*4+1] << 16)
               | ((uint32_t)src[i*4+2] << 8)  | ((uint32_t)src[i*4+3]);
    }
}

/** @brief Words to big endian bytes. */
static void s_get_bytes(const uint32_t *src, uint8_t *dst, uint32_t words)
{
    for (uint32_t i = 0; i < words; i++) {
        dst[i*4+0] = (uint8_t)(src[i] >> 24);
        dst[i*4+1] = (uint8_t)(src[i] >> 16);
        dst[i*4+2] = (uint8_t)(src[i] >> 8);
        dst[i*4+3] = (uint8_t)(src[i]);
    }
}

static void s_zero(void *p, uint32_t n)
{
    volatile uint8_t *b = (volatile uint8_t *)p;
    for (uint32_t i = 0; i < n; i++) {
        b[i] = 0;
    }
}

/** @brief The algorithm of a live context, or 0. */
static uint32_t s_alg_of(const sha2_stream_t *s, uint32_t hmac)
{
    uint32_t alg;

    if (s == NULL || (s->flags & SHA2_MAGIC_MASK) != SHA2_MAGIC) {
        return 0;
    }
    if ((s->flags & SHA2_HMAC) != hmac) {
        return 0;
    }
    alg = (s->flags >> SHA2_ALG_SHIFT) & 0xFF;
    return (alg == ALG_SHA256 || alg == ALG_SHA512) ? alg : 0;
}

/**
 * @brief Hash whole blocks into a context. The engine starts from the H
 *        words (and with HMAC, from the key block) the first time, and
 *        from the context's own state, put back into HOUT, after that.
 * @param key the key block words for HMAC pass 1, NULL for a plain hash.
 */
static int s_blocks(sha2_stream_t *s, const s_alg_t *a, const uint8_t *data,
                    uint32_t nblk, const uint32_t *key)
{
    int rc = s_load_constants();

    while (rc == SHA_OK && nblk > 0) {
        uint32_t n = nblk < a->msg_cap ? nblk : a->msg_cap;
        uint32_t opt2;

        s_put_words(HASH_MSG_BUF, data, n * a->msg_words);
        if (s->flags & SHA2_STARTED) {
            for (uint32_t i = 0; i < a->st_words; i++) {
                HASH_OUT_BUF[i] = s->state[i];
            }
            opt2 = 0;
        } else {
            if (key != NULL) {
                for (uint32_t i = 0; i < a->msg_words; i++) {
                    HASH_KEY_BUF[i] = key[i];
                }
            }
            opt2 = HASH_OPT2_IFSTART;
        }
        rc = s_run(key != NULL ? a->fn_pass1 : a->fn_hash, n - 1, opt2);
        for (uint32_t i = 0; i < a->st_words; i++) {
            s->state[i] = HASH_OUT_BUF[i];
        }
        s->flags |= SHA2_STARTED;
        data += n * a->block;
        nblk -= n;
    }
    return rc;
}

static void s_init(sha2_stream_t *s, uint32_t alg, uint32_t hmac)
{
    s_zero(s, sizeof(*s));
    s->flags = SHA2_MAGIC | (alg << SHA2_ALG_SHIFT) | hmac;
}

static int s_update(sha2_stream_t *s, uint32_t hmac, const uint8_t *data, uint32_t len,
                    const uint32_t *key)
{
    uint32_t alg = s_alg_of(s, hmac);
    const s_alg_t *a = &s_algs[alg];
    int rc = SHA_OK;

    if (alg == 0) {
        return SHA_ERR_STATE;
    }
    if (len == 0) {
        return SHA_OK;
    }
    if (data == NULL) {
        return SHA_ERR_ARG;
    }
    s->total += len;

    /* top up a partial block first */
    if (s->buf_len > 0) {
        uint32_t take = a->block - s->buf_len;
        if (take > len) {
            take = len;
        }
        for (uint32_t i = 0; i < take; i++) {
            s->buf[s->buf_len + i] = data[i];
        }
        s->buf_len += take;
        data += take;
        len -= take;
        if (s->buf_len < a->block) {
            return SHA_OK;
        }
        rc = s_blocks(s, a, s->buf, 1, key);
        s->buf_len = 0;
    }

    /* whole blocks straight from the caller's buffer */
    if (rc == SHA_OK && len >= a->block) {
        uint32_t nblk = len / a->block;
        rc = s_blocks(s, a, data, nblk, key);
        data += nblk * a->block;
        len -= nblk * a->block;
    }

    if (rc != SHA_OK) {
        /* the state is lost, so the context can give no digest */
        s_zero(s, sizeof(*s));
        return rc;
    }
    for (uint32_t i = 0; i < len; i++) {
        s->buf[i] = data[i];
    }
    s->buf_len = len;
    return SHA_OK;
}

/**
 * @brief Pad and hash the last block or two. extra is the number of bytes
 *        hashed before the message (one block for the HMAC inner hash).
 *        Leaves the final state in s->state.
 */
static int s_finish(sha2_stream_t *s, const s_alg_t *a, uint32_t extra, const uint32_t *key)
{
    uint64_t lo = (s->total + extra) << 3;
    uint64_t hi = (s->total + extra) >> 61;
    uint32_t n = s->buf_len, i;
    int rc;

    s->buf[n++] = 0x80;
    if (n > a->block - a->len_bytes) {
        for (i = n; i < a->block; i++) {
            s->buf[i] = 0;
        }
        rc = s_blocks(s, a, s->buf, 1, key);
        if (rc != SHA_OK) {
            return rc;
        }
        n = 0;
    }
    for (i = n; i < a->block - 8; i++) {
        s->buf[i] = 0;
    }
    for (i = 0; i < 8; i++) {
        s->buf[a->block - 1 - i] = (uint8_t)(lo >> (8 * i));
        if (a->len_bytes == 16) {
            s->buf[a->block - 9 - i] = (uint8_t)(hi >> (8 * i));
        }
    }
    return s_blocks(s, a, s->buf, 1, key);
}

static int s_final(sha2_stream_t *s, uint8_t *digest)
{
    uint32_t alg = s_alg_of(s, 0);
    int rc;

    if (alg == 0) {
        return SHA_ERR_STATE;
    }
    if (digest == NULL) {
        return SHA_ERR_ARG;
    }
    rc = s_finish(s, &s_algs[alg], 0, NULL);
    if (rc == SHA_OK) {
        s_get_bytes(s->state, digest, s_algs[alg].st_words);
    }
    s_zero(s, sizeof(*s));
    return rc;
}

/**
 * @brief The key block of an HMAC: the key padded with zeros, or the hash
 *        of the key when it is longer than one block (RFC 2104 section 2).
 *        A long key goes through the engine's key hash function, at most
 *        one LKEY segment of padded blocks per start, and the hash is
 *        left in the KEY segment.
 */
static int s_hmac_key(const s_alg_t *a, const uint8_t *key, uint32_t key_len, uint32_t *kw)
{
    uint32_t i;

    for (i = 0; i < a->msg_words; i++) {
        kw[i] = 0;
    }
    if (key_len <= a->block) {
        for (i = 0; i < key_len; i++) {
            kw[i / 4] |= (uint32_t)key[i] << (24 - 8 * (i % 4));
        }
        return SHA_OK;
    }

    {
        uint64_t bits = (uint64_t)key_len << 3;
        uint32_t nblk = (key_len + 1 + a->len_bytes + a->block - 1) / a->block;
        uint32_t total = nblk * a->block, done = 0;
        int rc = s_load_constants();

        while (rc == SHA_OK && done < nblk) {
            uint32_t n = nblk - done < a->lkey_cap ? nblk - done : a->lkey_cap;
            /* the padded key, byte by byte: key, 0x80, zeros, bit length */
            for (uint32_t w = 0; w < n * a->msg_words; w++) {
                uint32_t v = 0;
                for (uint32_t k = 0; k < 4; k++) {
                    uint32_t p = done * a->block + w * 4 + k;
                    uint32_t c;
                    if (p < key_len) {
                        c = key[p];
                    } else if (p == key_len) {
                        c = 0x80;
                    } else if (p >= total - 8) {
                        c = (uint32_t)(bits >> (8 * (total - 1 - p))) & 0xFF;
                    } else {
                        c = 0;
                    }
                    v = (v << 8) | c;
                }
                HASH_LKEY_BUF[w] = v;
            }
            /* after the first start the state comes back from the KEY
             * segment, where the last start wrote it */
            rc = s_run(a->fn_keyhash, n - 1, done == 0 ? HASH_OPT2_IFSTART : 0);
            done += n;
        }
        if (rc != SHA_OK) {
            return rc;
        }
        for (i = 0; i < a->st_words; i++) {
            kw[i] = HASH_KEY_BUF[i];
        }
        return SHA_OK;
    }
}

static int s_hmac_init(sha2_stream_t *s, uint32_t *kw, uint32_t alg,
                       const uint8_t *key, uint32_t key_len)
{
    int rc;

    if (s == NULL || kw == NULL || (key_len > 0 && key == NULL)) {
        return SHA_ERR_ARG;
    }
    s_init(s, alg, SHA2_HMAC);
    rc = s_hmac_key(&s_algs[alg], key, key_len, kw);
    if (rc != SHA_OK) {
        s_zero(s, sizeof(*s));
    }
    return rc;
}

static int s_hmac_final(sha2_stream_t *s, uint32_t *kw, uint32_t kwords, uint8_t *mac)
{
    uint32_t alg = s_alg_of(s, SHA2_HMAC);
    const s_alg_t *a = &s_algs[alg];
    int rc;

    if (alg == 0) {
        return SHA_ERR_STATE;
    }
    if (mac == NULL) {
        return SHA_ERR_ARG;
    }
    /* the inner hash: pass 1 over the key block XOR 0x36 and the message */
    rc = s_finish(s, a, a->block, kw);
    if (rc == SHA_OK) {
        /* the outer hash: pass 2 over the key block XOR 0x5C and the inner
         * hash read from HOUT, padded by the engine */
        for (uint32_t i = 0; i < a->st_words; i++) {
            HASH_OUT_BUF[i] = s->state[i];
        }
        for (uint32_t i = 0; i < a->msg_words; i++) {
            HASH_KEY_BUF[i] = kw[i];
        }
        rc = s_run(a->fn_pass2, 0, HASH_OPT2_IFSTART);
    }
    if (rc == SHA_OK) {
        uint32_t out[16];
        for (uint32_t i = 0; i < a->st_words; i++) {
            out[i] = HASH_OUT_BUF[i];
        }
        s_get_bytes(out, mac, a->st_words);
        s_zero(out, sizeof(out));
    }
    /* the key block was also left in the KEY segment */
    for (uint32_t i = 0; i < a->msg_words; i++) {
        HASH_KEY_BUF[i] = 0;
    }
    s_zero(s, sizeof(*s));
    s_zero(kw, kwords * 4);
    return rc;
}

/* SHA-256 */

/** @req REQ-DABAO-SHA-0003 */
int sha256_stream_init(sha256_ctx_t *ctx)
{
    if (ctx == NULL) {
        return SHA_ERR_ARG;
    }
    s_init(&ctx->s, ALG_SHA256, 0);
    return SHA_OK;
}

/** @req REQ-DABAO-SHA-0003 */
int sha256_stream_update(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    return s_update(ctx ? &ctx->s : NULL, 0, data, len, NULL);
}

/** @req REQ-DABAO-SHA-0003 */
int sha256_stream_final(sha256_ctx_t *ctx, uint8_t *digest)
{
    return s_final(ctx ? &ctx->s : NULL, digest);
}

/* SHA-512 */

/** @req REQ-DABAO-SHA-0004 */
int sha512_stream_init(sha512_ctx_t *ctx)
{
    if (ctx == NULL) {
        return SHA_ERR_ARG;
    }
    s_init(&ctx->s, ALG_SHA512, 0);
    return SHA_OK;
}

/** @req REQ-DABAO-SHA-0004 */
int sha512_stream_update(sha512_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    return s_update(ctx ? &ctx->s : NULL, 0, data, len, NULL);
}

/** @req REQ-DABAO-SHA-0004 */
int sha512_stream_final(sha512_ctx_t *ctx, uint8_t *digest)
{
    return s_final(ctx ? &ctx->s : NULL, digest);
}

/** @req REQ-DABAO-SHA-0004 */
int sha512_hash(const uint8_t *msg, uint32_t msg_len, uint8_t *digest)
{
    sha512_ctx_t c;
    int rc;

    if (digest == NULL || (msg_len > 0 && msg == NULL)) {
        return SHA_ERR_ARG;
    }
    sha512_stream_init(&c);
    rc = sha512_stream_update(&c, msg, msg_len);
    if (rc != SHA_OK) {
        return rc;
    }
    return sha512_stream_final(&c, digest);
}

/* HMAC */

/** @req REQ-DABAO-SHA-0005 */
int hmac_sha256_init(hmac_sha256_ctx_t *ctx, const uint8_t *key, uint32_t key_len)
{
    return s_hmac_init(ctx ? &ctx->s : NULL, ctx ? ctx->key : NULL, ALG_SHA256, key, key_len);
}

/** @req REQ-DABAO-SHA-0005 */
int hmac_sha256_update(hmac_sha256_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    return s_update(ctx ? &ctx->s : NULL, SHA2_HMAC, data, len, ctx ? ctx->key : NULL);
}

/** @req REQ-DABAO-SHA-0005 */
int hmac_sha256_final(hmac_sha256_ctx_t *ctx, uint8_t *mac)
{
    if (ctx == NULL) {
        return SHA_ERR_STATE;
    }
    return s_hmac_final(&ctx->s, ctx->key, 16, mac);
}

/** @req REQ-DABAO-SHA-0005 */
int hmac_sha256(const uint8_t *key, uint32_t key_len, const uint8_t *msg, uint32_t msg_len,
                uint8_t *mac)
{
    hmac_sha256_ctx_t c;
    int rc;

    if (mac == NULL || (msg_len > 0 && msg == NULL)) {
        return SHA_ERR_ARG;
    }
    rc = hmac_sha256_init(&c, key, key_len);
    if (rc == SHA_OK) {
        rc = hmac_sha256_update(&c, msg, msg_len);
    }
    if (rc == SHA_OK) {
        rc = hmac_sha256_final(&c, mac);
    }
    s_zero(&c, sizeof(c));
    return rc;
}

/** @req REQ-DABAO-SHA-0006 */
int hmac_sha512_init(hmac_sha512_ctx_t *ctx, const uint8_t *key, uint32_t key_len)
{
    return s_hmac_init(ctx ? &ctx->s : NULL, ctx ? ctx->key : NULL, ALG_SHA512, key, key_len);
}

/** @req REQ-DABAO-SHA-0006 */
int hmac_sha512_update(hmac_sha512_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    return s_update(ctx ? &ctx->s : NULL, SHA2_HMAC, data, len, ctx ? ctx->key : NULL);
}

/** @req REQ-DABAO-SHA-0006 */
int hmac_sha512_final(hmac_sha512_ctx_t *ctx, uint8_t *mac)
{
    if (ctx == NULL) {
        return SHA_ERR_STATE;
    }
    return s_hmac_final(&ctx->s, ctx->key, 32, mac);
}

/** @req REQ-DABAO-SHA-0006 */
int hmac_sha512(const uint8_t *key, uint32_t key_len, const uint8_t *msg, uint32_t msg_len,
                uint8_t *mac)
{
    hmac_sha512_ctx_t c;
    int rc;

    if (mac == NULL || (msg_len > 0 && msg == NULL)) {
        return SHA_ERR_ARG;
    }
    rc = hmac_sha512_init(&c, key, key_len);
    if (rc == SHA_OK) {
        rc = hmac_sha512_update(&c, msg, msg_len);
    }
    if (rc == SHA_OK) {
        rc = hmac_sha512_final(&c, mac);
    }
    s_zero(&c, sizeof(c));
    return rc;
}
