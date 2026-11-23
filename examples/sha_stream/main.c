/*
 * main.c: streaming SHA-256 and SHA-512 on the SCE hash engine for baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives the streaming calls of the SDK SHA driver (sha_stream.c), which
 * run the engine's SHA-256 and SHA-512 functions block by block and keep
 * the running state in a context the caller owns.
 *
 * Test vectors: NIST FIPS 180-2 appendices B and C, also in the NIST
 * CSRC "Examples with intermediate values" for SHA-256 and SHA-512: the
 * empty message, "abc", the 448 bit message abcdbcde...nopq, the 896 bit
 * message abcdefgh...nopqrstu and one million "a". The digests below are
 * those published values.
 *
 * Checks, each line ending in PASS or FAIL:
 *   every vector through init, one update and final, and through
 *   sha512_hash for SHA-512, and the old sha256_hash on the short ones;
 *   one million "a" fed in uneven pieces (0, 1, 63, 64, 65 ... 4096
 *   bytes), SHA-256 and SHA-512 at the same time with their updates in
 *   turn, so each context's state goes through the engine between the
 *   other's blocks;
 *   two SHA-256 contexts fed a byte at a time in turn, with sha256_init
 *   run in the middle (it loads only the SHA-256 constants);
 *   the 896 bit message split at every point into two updates and at
 *   many points into three;
 *   a 600 byte message split at every point against the one call result;
 *   the return codes for a NULL buffer, a zero length and a finished
 *   context.
 */

#include "bao.h"
#include "hardware/sha.h"

static const char m448[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
static const char m896[] = "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu";

/* FIPS 180-2 digests */
static const uint8_t d256_empty[32] = {
    0xe3, 0xb0, 0xc4, 0x42, 0x98, 0xfc, 0x1c, 0x14, 0x9a, 0xfb, 0xf4, 0xc8, 0x99, 0x6f, 0xb9, 0x24,
    0x27, 0xae, 0x41, 0xe4, 0x64, 0x9b, 0x93, 0x4c, 0xa4, 0x95, 0x99, 0x1b, 0x78, 0x52, 0xb8, 0x55,
};
static const uint8_t d256_abc[32] = {
    0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
    0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
};
static const uint8_t d256_448[32] = {
    0x24, 0x8d, 0x6a, 0x61, 0xd2, 0x06, 0x38, 0xb8, 0xe5, 0xc0, 0x26, 0x93, 0x0c, 0x3e, 0x60, 0x39,
    0xa3, 0x3c, 0xe4, 0x59, 0x64, 0xff, 0x21, 0x67, 0xf6, 0xec, 0xed, 0xd4, 0x19, 0xdb, 0x06, 0xc1,
};
static const uint8_t d256_896[32] = {
    0xcf, 0x5b, 0x16, 0xa7, 0x78, 0xaf, 0x83, 0x80, 0x03, 0x6c, 0xe5, 0x9e, 0x7b, 0x04, 0x92, 0x37,
    0x0b, 0x24, 0x9b, 0x11, 0xe8, 0xf0, 0x7a, 0x51, 0xaf, 0xac, 0x45, 0x03, 0x7a, 0xfe, 0xe9, 0xd1,
};
static const uint8_t d256_million[32] = {
    0xcd, 0xc7, 0x6e, 0x5c, 0x99, 0x14, 0xfb, 0x92, 0x81, 0xa1, 0xc7, 0xe2, 0x84, 0xd7, 0x3e, 0x67,
    0xf1, 0x80, 0x9a, 0x48, 0xa4, 0x97, 0x20, 0x0e, 0x04, 0x6d, 0x39, 0xcc, 0xc7, 0x11, 0x2c, 0xd0,
};
static const uint8_t d512_empty[64] = {
    0xcf, 0x83, 0xe1, 0x35, 0x7e, 0xef, 0xb8, 0xbd, 0xf1, 0x54, 0x28, 0x50, 0xd6, 0x6d, 0x80, 0x07,
    0xd6, 0x20, 0xe4, 0x05, 0x0b, 0x57, 0x15, 0xdc, 0x83, 0xf4, 0xa9, 0x21, 0xd3, 0x6c, 0xe9, 0xce,
    0x47, 0xd0, 0xd1, 0x3c, 0x5d, 0x85, 0xf2, 0xb0, 0xff, 0x83, 0x18, 0xd2, 0x87, 0x7e, 0xec, 0x2f,
    0x63, 0xb9, 0x31, 0xbd, 0x47, 0x41, 0x7a, 0x81, 0xa5, 0x38, 0x32, 0x7a, 0xf9, 0x27, 0xda, 0x3e,
};
static const uint8_t d512_abc[64] = {
    0xdd, 0xaf, 0x35, 0xa1, 0x93, 0x61, 0x7a, 0xba, 0xcc, 0x41, 0x73, 0x49, 0xae, 0x20, 0x41, 0x31,
    0x12, 0xe6, 0xfa, 0x4e, 0x89, 0xa9, 0x7e, 0xa2, 0x0a, 0x9e, 0xee, 0xe6, 0x4b, 0x55, 0xd3, 0x9a,
    0x21, 0x92, 0x99, 0x2a, 0x27, 0x4f, 0xc1, 0xa8, 0x36, 0xba, 0x3c, 0x23, 0xa3, 0xfe, 0xeb, 0xbd,
    0x45, 0x4d, 0x44, 0x23, 0x64, 0x3c, 0xe8, 0x0e, 0x2a, 0x9a, 0xc9, 0x4f, 0xa5, 0x4c, 0xa4, 0x9f,
};
static const uint8_t d512_448[64] = {
    0x20, 0x4a, 0x8f, 0xc6, 0xdd, 0xa8, 0x2f, 0x0a, 0x0c, 0xed, 0x7b, 0xeb, 0x8e, 0x08, 0xa4, 0x16,
    0x57, 0xc1, 0x6e, 0xf4, 0x68, 0xb2, 0x28, 0xa8, 0x27, 0x9b, 0xe3, 0x31, 0xa7, 0x03, 0xc3, 0x35,
    0x96, 0xfd, 0x15, 0xc1, 0x3b, 0x1b, 0x07, 0xf9, 0xaa, 0x1d, 0x3b, 0xea, 0x57, 0x78, 0x9c, 0xa0,
    0x31, 0xad, 0x85, 0xc7, 0xa7, 0x1d, 0xd7, 0x03, 0x54, 0xec, 0x63, 0x12, 0x38, 0xca, 0x34, 0x45,
};
static const uint8_t d512_896[64] = {
    0x8e, 0x95, 0x9b, 0x75, 0xda, 0xe3, 0x13, 0xda, 0x8c, 0xf4, 0xf7, 0x28, 0x14, 0xfc, 0x14, 0x3f,
    0x8f, 0x77, 0x79, 0xc6, 0xeb, 0x9f, 0x7f, 0xa1, 0x72, 0x99, 0xae, 0xad, 0xb6, 0x88, 0x90, 0x18,
    0x50, 0x1d, 0x28, 0x9e, 0x49, 0x00, 0xf7, 0xe4, 0x33, 0x1b, 0x99, 0xde, 0xc4, 0xb5, 0x43, 0x3a,
    0xc7, 0xd3, 0x29, 0xee, 0xb6, 0xdd, 0x26, 0x54, 0x5e, 0x96, 0xe5, 0x5b, 0x87, 0x4b, 0xe9, 0x09,
};
static const uint8_t d512_million[64] = {
    0xe7, 0x18, 0x48, 0x3d, 0x0c, 0xe7, 0x69, 0x64, 0x4e, 0x2e, 0x42, 0xc7, 0xbc, 0x15, 0xb4, 0x63,
    0x8e, 0x1f, 0x98, 0xb1, 0x3b, 0x20, 0x44, 0x28, 0x56, 0x32, 0xa8, 0x03, 0xaf, 0xa9, 0x73, 0xeb,
    0xde, 0x0f, 0xf2, 0x44, 0x87, 0x7e, 0xa6, 0x0a, 0x4c, 0xb0, 0x43, 0x2c, 0xe5, 0x77, 0xc3, 0x1b,
    0xeb, 0x00, 0x9c, 0x5c, 0x2c, 0x49, 0xaa, 0x2e, 0x4e, 0xad, 0xb2, 0x17, 0xad, 0x8c, 0xc0, 0x9b,
};
static int passed, total;
static uint8_t abuf[4096], big[600];

static void hex(const uint8_t *d, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        mini_printf("%02x", d[i]);
}

static int same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (a[i] != b[i])
            return 0;
    return 1;
}

static void result(int ok)
{
    total++;
    if (ok) {
        passed++;
        mini_printf(" PASS\r\n");
    } else {
        mini_printf(" FAIL\r\n");
    }
}

/* one line: the name, the digest got, PASS or FAIL against want */
static void show(const char *name, const uint8_t *got, const uint8_t *want, uint32_t n)
{
    mini_printf("%s: ", name);
    hex(got, n);
    result(same(got, want, n));
}

static void s256(const char *name, const void *m, uint32_t len, const uint8_t *want)
{
    sha256_ctx_t c;
    uint8_t d[32];
    int rc = sha256_stream_init(&c);
    rc |= sha256_stream_update(&c, (const uint8_t *)m, len);
    rc |= sha256_stream_final(&c, d);
    if (rc)
        d[0] ^= 0xFF;
    show(name, d, want, 32);
}

static void s512(const char *name, const void *m, uint32_t len, const uint8_t *want)
{
    sha512_ctx_t c;
    uint8_t d[64];
    int rc = sha512_stream_init(&c);
    rc |= sha512_stream_update(&c, (const uint8_t *)m, len);
    rc |= sha512_stream_final(&c, d);
    if (rc)
        d[0] ^= 0xFF;
    show(name, d, want, 64);
}

static void h512(const char *name, const void *m, uint32_t len, const uint8_t *want)
{
    uint8_t d[64];
    if (sha512_hash((const uint8_t *)m, len, d))
        d[0] ^= 0xFF;
    show(name, d, want, 64);
}

/* uneven piece sizes, every one at most sizeof(abuf) */
static const uint16_t pieces[] = {1, 63, 64, 65, 0, 127, 128, 129, 1000, 3333, 7, 4096, 2, 513, 255, 31};

static void million(void)
{
    sha256_ctx_t c2;
    sha512_ctx_t c5;
    uint8_t d2[32], d5[64];
    uint32_t left2 = 1000000, left5 = 1000000, k = 0;
    const uint32_t np = sizeof(pieces) / sizeof(pieces[0]);
    int rc = 0;

    rc |= sha256_stream_init(&c2);
    rc |= sha512_stream_init(&c5);
    /* the two contexts take different piece sizes, in turn */
    while (left2 || left5) {
        uint32_t n2 = pieces[k % np], n5 = pieces[(k + 5) % np];
        if (n2 > left2) n2 = left2;
        if (n5 > left5) n5 = left5;
        rc |= sha256_stream_update(&c2, abuf, n2);
        rc |= sha512_stream_update(&c5, abuf, n5);
        left2 -= n2;
        left5 -= n5;
        k++;
    }
    rc |= sha256_stream_final(&c2, d2);
    rc |= sha512_stream_final(&c5, d5);
    mini_printf("million a in %u rounds of pieces, return codes: %d", (unsigned)k, rc);
    result(rc == 0);
    show("SHA-256 million a in pieces", d2, d256_million, 32);
    show("SHA-512 million a in pieces", d5, d512_million, 64);
}

int main(void)
{
    uint32_t i, s, bad;
    uint8_t d[64], want[64];

    bao_init();
    mini_printf("\r\nsha_stream start\r\n");
    for (i = 0; i < sizeof(abuf); i++)
        abuf[i] = 'a';
    for (i = 0; i < sizeof(big); i++)
        big[i] = (uint8_t)(i * 13 + 5);

    s256("SHA-256 empty", "", 0, d256_empty);
    s256("SHA-256 abc", "abc", 3, d256_abc);
    s256("SHA-256 448 bit", m448, 56, d256_448);
    s256("SHA-256 896 bit", m896, 112, d256_896);
    s512("SHA-512 empty", "", 0, d512_empty);
    s512("SHA-512 abc", "abc", 3, d512_abc);
    s512("SHA-512 448 bit", m448, 56, d512_448);
    s512("SHA-512 896 bit", m896, 112, d512_896);
    h512("sha512_hash empty", "", 0, d512_empty);
    h512("sha512_hash abc", "abc", 3, d512_abc);
    h512("sha512_hash 896 bit", m896, 112, d512_896);

    sha256_init();
    sha256_hash((const uint8_t *)"abc", 3, d);
    show("sha256_hash abc after streaming", d, d256_abc, 32);
    sha256_hash((const uint8_t *)"", 0, d);
    show("sha256_hash empty", d, d256_empty, 32);

    million();

    {
        /* two SHA-256 contexts a byte at a time in turn */
        sha256_ctx_t a, b;
        uint8_t da[32], db[32];
        int rc = sha256_stream_init(&a) | sha256_stream_init(&b);
        for (i = 0; i < 112; i++) {
            if (i < 56)
                rc |= sha256_stream_update(&a, (const uint8_t *)m448 + i, 1);
            rc |= sha256_stream_update(&b, (const uint8_t *)m896 + i, 1);
            if (i == 60)
                sha256_init();
        }
        rc |= sha256_stream_final(&a, da);
        rc |= sha256_stream_final(&b, db);
        if (rc)
            da[0] ^= 0xFF;
        show("two SHA-256 contexts in turn, 448 bit", da, d256_448, 32);
        show("two SHA-256 contexts in turn, 896 bit", db, d256_896, 32);
    }

    {
        /* a SHA-256 and a SHA-512 context, 448 bit message, in turn */
        sha256_ctx_t a;
        sha512_ctx_t b;
        uint8_t da[32], db[64];
        int rc = sha256_stream_init(&a) | sha512_stream_init(&b);
        for (i = 0; i < 56; i += 8) {
            rc |= sha512_stream_update(&b, (const uint8_t *)m448 + i, 8);
            rc |= sha256_stream_update(&a, (const uint8_t *)m448 + i, 8);
        }
        rc |= sha512_stream_final(&b, db);
        rc |= sha256_stream_final(&a, da);
        if (rc)
            da[0] ^= 0xFF;
        show("SHA-256 and SHA-512 in turn, SHA-256", da, d256_448, 32);
        show("SHA-256 and SHA-512 in turn, SHA-512", db, d512_448, 64);
    }

    /* every two piece split of the 896 bit message */
    for (bad = 0, s = 0; s <= 112; s++) {
        sha256_ctx_t c;
        sha512_ctx_t c5;
        sha256_stream_init(&c);
        sha256_stream_update(&c, (const uint8_t *)m896, s);
        sha256_stream_update(&c, (const uint8_t *)m896 + s, 112 - s);
        sha256_stream_final(&c, d);
        bad += !same(d, d256_896, 32);
        sha512_stream_init(&c5);
        sha512_stream_update(&c5, (const uint8_t *)m896, s);
        sha512_stream_update(&c5, (const uint8_t *)m896 + s, 112 - s);
        sha512_stream_final(&c5, d);
        bad += !same(d, d512_896, 64);
    }
    mini_printf("896 bit message at all 113 split points, wrong: %u", (unsigned)bad);
    result(bad == 0);

    /* three pieces: s and a second cut further on */
    for (bad = 0, s = 0; s <= 112; s++) {
        uint32_t t = s + (s * 37 + 11) % (113 - s);
        sha256_ctx_t c;
        sha512_ctx_t c5;
        sha256_stream_init(&c);
        sha256_stream_update(&c, (const uint8_t *)m896, s);
        sha256_stream_update(&c, (const uint8_t *)m896 + s, t - s);
        sha256_stream_update(&c, (const uint8_t *)m896 + t, 112 - t);
        sha256_stream_final(&c, d);
        bad += !same(d, d256_896, 32);
        sha512_stream_init(&c5);
        sha512_stream_update(&c5, (const uint8_t *)m896, s);
        sha512_stream_update(&c5, (const uint8_t *)m896 + s, t - s);
        sha512_stream_update(&c5, (const uint8_t *)m896 + t, 112 - t);
        sha512_stream_final(&c5, d);
        bad += !same(d, d512_896, 64);
    }
    mini_printf("896 bit message in three pieces at 113 cuts, wrong: %u", (unsigned)bad);
    result(bad == 0);

    /* 600 bytes split at every point against the one call result */
    {
        sha256_ctx_t c;
        sha256_stream_init(&c);
        sha256_stream_update(&c, big, sizeof(big));
        sha256_stream_final(&c, want);
        mini_printf("SHA-256 600 bytes in one update: ");
        hex(want, 32);
        mini_printf("\r\n");
        for (bad = 0, s = 0; s <= sizeof(big); s++) {
            sha256_stream_init(&c);
            sha256_stream_update(&c, big, s);
            sha256_stream_update(&c, big + s, sizeof(big) - s);
            sha256_stream_final(&c, d);
            bad += !same(d, want, 32);
        }
        mini_printf("SHA-256 600 bytes at all 601 split points, wrong: %u", (unsigned)bad);
        result(bad == 0);
    }
    {
        sha512_ctx_t c;
        sha512_hash(big, sizeof(big), want);
        mini_printf("sha512_hash 600 bytes: ");
        hex(want, 64);
        mini_printf("\r\n");
        for (bad = 0, s = 0; s <= sizeof(big); s++) {
            sha512_stream_init(&c);
            sha512_stream_update(&c, big, s);
            sha512_stream_update(&c, big + s, sizeof(big) - s);
            sha512_stream_final(&c, d);
            bad += !same(d, want, 64);
        }
        mini_printf("SHA-512 600 bytes at all 601 split points, wrong: %u", (unsigned)bad);
        result(bad == 0);
    }

    {
        sha256_ctx_t c;
        int r1, r2, r3, r4;
        sha256_stream_init(&c);
        r1 = sha256_stream_update(&c, NULL, 0);
        r2 = sha256_stream_update(&c, NULL, 5);
        sha256_stream_init(&c);
        sha256_stream_final(&c, d);
        r3 = sha256_stream_update(&c, (const uint8_t *)"abc", 3);
        r4 = sha256_stream_final(&c, d);
        mini_printf("return codes: zero length %d, NULL %d, after final %d %d", r1, r2, r3, r4);
        result(r1 == SHA_OK && r2 == SHA_ERR_ARG && r3 == SHA_ERR_STATE && r4 == SHA_ERR_STATE);
    }

    mini_printf("[Done] sha_stream %d of %d passed\r\n", passed, total);
    while (1)
        __asm__ volatile ("wfi");
}
