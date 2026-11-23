/*
 * main.c: Ed25519 (RFC 8032, pure) for baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives the SDK's hardware_sign module (ed25519.c): SHA-512 on the SCE
 * hash engine, the point multiplications and additions on the PKE, the
 * square root of point decoding on the PKE's modular exponent and the
 * reductions mod L on the ALU divider.
 *
 * Test vectors: RFC 8032, "Edwards-Curve Digital Signature Algorithm
 * (EdDSA)", section 7.1, Ed25519 TEST 1 (empty message), TEST 2 (one
 * byte), TEST 3 (two bytes) and TEST SHA(abc) (the 64 byte SHA-512 of
 * "abc" as the message). The 300 byte message has no published vector;
 * its signature was worked out with tests/sign_ref.py and checked with the
 * cryptography package (OpenSSL), and is there to make the engine hash
 * more than one block.
 *
 * Checks, each line ending in PASS or FAIL:
 *   the public key from each seed, the exact signature, verify, and
 *   rejects of a changed message and of one bit flipped in R and in S;
 *   S not canonical (S + L, L, all ones), public keys with y = p, with no
 *   x for their y, and with x = 0 and the sign bit set; R that does not
 *   decode; the wrong public key; the identity as public key; bad
 *   arguments; TEST 3 signed with the message inside the signature
 *   buffer;
 *   no copy of the secret scalar or the prefix left on the stack below
 *   the caller after a signature.
 * The time a sign and a verify take is printed with millis().
 */

#include "bao.h"
#include "hardware/sha.h"
#include "hardware/sign.h"

static uint32_t s_pass, s_total;

static void result(bool ok)
{
    s_total++;
    if (ok) {
        s_pass++;
        mini_printf(" PASS\r\n");
    } else {
        mini_printf(" FAIL\r\n");
    }
}

static void hex(const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        mini_printf("%02x", b[i]);
}

static uint32_t unhex(const char *s, uint8_t *out)
{
    uint32_t n = 0;
    while (s[0] && s[1]) {
        uint8_t v = 0;
        for (uint32_t j = 0; j < 2; j++) {
            char c = s[j];
            v = (uint8_t)(v << 4);
            if (c >= '0' && c <= '9') v |= (uint8_t)(c - '0');
            else if (c >= 'a' && c <= 'f') v |= (uint8_t)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= (uint8_t)(c - 'A' + 10);
        }
        out[n++] = v;
        s += 2;
    }
    return n;
}

static bool same(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        if (a[i] != b[i])
            return false;
    }
    return true;
}

/* True when the n byte pattern appears anywhere in the 4 KB of stack
 * below this function's frame, where the frames of the call just made
 * were. */
static bool __attribute__((noinline)) stack_has(const uint8_t *pat, uint32_t n)
{
    const uint8_t *top = (const uint8_t *)__builtin_frame_address(0);
    const volatile uint8_t *p = top - 4096;
    for (; p + n <= top; p++) {
        uint32_t i;
        for (i = 0; i < n && p[i] == pat[i]; i++) {
        }
        if (i == n)
            return true;
    }
    return false;
}

typedef struct {
    const char *name;
    const char *seed;
    const char *pub;
    const char *msg;
    const char *sig;
} vec_t;

/* RFC 8032 section 7.1 */
static const vec_t s_vec[] = {
    { "TEST 1",
      "9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
      "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a",
      "",
      "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46b"
      "d25bf5f0595bbe24655141438e7a100b" },
    { "TEST 2",
      "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
      "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c",
      "72",
      "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c"
      "387b2eaeb4302aeeb00d291612bb0c00" },
    { "TEST 3",
      "c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
      "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025",
      "af82",
      "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc659"
      "4a7c15e9716ed28dc027beceea1ec40a" },
    { "TEST SHA(abc)",
      "833fe62409237b9d62ec77587520911e9a759cec1d19755b7da901b96dca3d42",
      "ec172b93ad5e563bf4932c70e1245034c35467ef2efd4d64ebf819683467e2bf",
      "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a"
      "2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f",
      "dc2a4459e7369633a52b1bf277839a00201009a3efbf3ecb69bea2186c26b58909351fc9ac90b3ecfdfbc7c66431e030"
      "3dca179c138ac17ad9bef1177331a704" },
};

/* L, little endian, as RFC 8032 encodes scalars */
static const char ED_L[] =
    "edd3f55c1a631258d69cf7a2def9de1400000000000000000000000000000010";

static const char LONG_SIG[] =
    "b290999d1d1db359d629003fced156ddde5590e02cecd0f42b3b826306276193"
    "f11c6238cc10017f78e8d70bb496f1917b82a1bf31ba6f57e06a6f497cacea00";

static void run_vector(const vec_t *v)
{
    uint8_t seed[32], pub[32], want[64], sig[64], m[64], p2[32];
    uint32_t ml;
    int rc, a, b, c;

    unhex(v->seed, seed);
    unhex(v->pub, p2);
    unhex(v->sig, want);
    ml = unhex(v->msg, m);

    rc = ed25519_public_key(seed, pub);
    mini_printf("Ed25519 %s public key: ", v->name);
    hex(pub, 32);
    result(rc == SIGN_OK && same(pub, p2, 32));

    rc = ed25519_sign(seed, m, ml, sig);
    mini_printf("Ed25519 %s sign: ", v->name);
    hex(sig, 64);
    result(rc == SIGN_OK && same(sig, want, 64));

    rc = ed25519_verify(pub, m, ml, sig);
    mini_printf("Ed25519 %s verify: %d", v->name, rc);
    result(rc == SIGN_OK);

    if (ml == 0) {
        m[0] = 0;
        a = ed25519_verify(pub, m, 1, sig);
    } else {
        m[0] ^= 1u;
        a = ed25519_verify(pub, m, ml, sig);
        m[0] ^= 1u;
    }
    sig[0] ^= 1u;
    b = ed25519_verify(pub, m, ml, sig);
    sig[0] ^= 1u;
    sig[32] ^= 1u;
    c = ed25519_verify(pub, m, ml, sig);
    mini_printf("Ed25519 %s changed message, one bit flipped in R, in S: %d %d %d",
                v->name, a, b, c);
    result(a == SIGN_BAD && b == SIGN_BAD && c == SIGN_BAD);
}

int main(void)
{
    uint8_t seed[32], pub[32], pub2[32], sig[64], bad[64], el[32], h[64], m[300], k[32];
    uint64_t t0, t1, t2;
    int rc, a, b, c, d;

    bao_init();
    mini_printf("\r\ned25519 start\r\n");

    for (uint32_t i = 0; i < sizeof(s_vec) / sizeof(s_vec[0]); i++)
        run_vector(&s_vec[i]);

    /* TEST 1 key and signature for the rejects */
    unhex(s_vec[0].seed, seed);
    unhex(s_vec[0].pub, pub);
    unhex(s_vec[0].sig, sig);
    unhex(s_vec[1].pub, pub2);
    unhex(ED_L, el);

    /* S + L: the same S mod L, so the equation alone would accept it */
    for (uint32_t i = 0; i < 64; i++)
        bad[i] = sig[i];
    {
        uint32_t cy = 0;
        for (uint32_t i = 0; i < 32; i++) {
            uint32_t t = (uint32_t)bad[32 + i] + el[i] + cy;
            bad[32 + i] = (uint8_t)t;
            cy = t >> 8;
        }
    }
    a = ed25519_verify(pub, NULL, 0, bad);
    for (uint32_t i = 0; i < 32; i++)
        bad[32 + i] = el[i];
    b = ed25519_verify(pub, NULL, 0, bad);
    for (uint32_t i = 0; i < 32; i++)
        bad[32 + i] = 0xFF;
    c = ed25519_verify(pub, NULL, 0, bad);
    mini_printf("S not canonical: S + L, L, all ones: %d %d %d", a, b, c);
    result(a == SIGN_BAD && b == SIGN_BAD && c == SIGN_BAD);

    /* public keys that do not decode */
    unhex("edffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f", k);
    a = ed25519_verify(k, NULL, 0, sig);
    unhex("0200000000000000000000000000000000000000000000000000000000000000", k);
    b = ed25519_verify(k, NULL, 0, sig);
    unhex("0100000000000000000000000000000000000000000000000000000000000080", k);
    c = ed25519_verify(k, NULL, 0, sig);
    mini_printf("public key with y = p, y = 2 (no x), x = 0 with the sign bit: %d %d %d", a, b, c);
    result(a == SIGN_ERR_KEY && b == SIGN_ERR_KEY && c == SIGN_ERR_KEY);

    /* R that does not decode, the wrong key, the identity as key */
    for (uint32_t i = 0; i < 64; i++)
        bad[i] = sig[i];
    unhex("0200000000000000000000000000000000000000000000000000000000000000", bad);
    a = ed25519_verify(pub, NULL, 0, bad);
    b = ed25519_verify(pub2, NULL, 0, sig);
    unhex("0100000000000000000000000000000000000000000000000000000000000000", k);
    c = ed25519_verify(k, NULL, 0, sig);
    mini_printf("R with no x, TEST 2 key on TEST 1 signature, identity as key: %d %d %d", a, b, c);
    result(a == SIGN_BAD && b == SIGN_BAD && c == SIGN_BAD);

    a = ed25519_sign(NULL, NULL, 0, bad);
    b = ed25519_sign(seed, NULL, 1, bad);
    c = ed25519_verify(pub, NULL, 0, NULL);
    d = ed25519_public_key(seed, NULL);
    mini_printf("NULL seed, NULL message of length 1, NULL signature, NULL key out: %d %d %d %d",
                a, b, c, d);
    result(a == SIGN_ERR_ARG && b == SIGN_ERR_ARG && c == SIGN_ERR_ARG && d == SIGN_ERR_ARG);

    /* 300 bytes, three SHA-512 blocks with R and A in front */
    for (uint32_t i = 0; i < sizeof(m); i++)
        m[i] = (uint8_t)(i * 7u);
    unhex(LONG_SIG, bad);
    t0 = millis();
    rc = ed25519_sign(seed, m, sizeof(m), sig);
    t1 = millis();
    a = ed25519_verify(pub, m, sizeof(m), sig);
    t2 = millis();
    m[299] ^= 0x80u;
    b = ed25519_verify(pub, m, sizeof(m), sig);
    mini_printf("Ed25519 time: sign %u ms, verify %u ms\r\n", (unsigned)(t1 - t0), (unsigned)(t2 - t1));
    mini_printf("300 byte message: sign, verify, last bit flipped: %d %d %d", rc, a, b);
    result(rc == SIGN_OK && same(sig, bad, 64) && a == SIGN_OK && b == SIGN_BAD);

    /* TEST 3 with its message inside the signature buffer */
    {
        uint8_t want[64];
        unhex(s_vec[2].seed, k);
        unhex(s_vec[2].sig, want);
        unhex(s_vec[2].msg, bad);
        rc = ed25519_sign(k, bad, 2, bad);
        mini_printf("TEST 3 signed with the message in the signature buffer: %d", rc);
        result(rc == SIGN_OK && same(bad, want, 64));
    }

    /* Last: TEST 1 again, then look for the expanded key below this frame.
     * h is SHA-512(seed): its first half clamped is the secret scalar, its
     * second half the prefix. The SCE RAM is left cleared for the host. */
    a = sha512_hash(seed, 32, h);
    rc = ed25519_sign(seed, NULL, 0, sig);
    b = 0;
    for (uint32_t i = 0; i < 64; i += 16)
        b |= stack_has(h + i, 16);
    for (uint32_t i = 0; i < 32; i++)
        k[i] = h[i];
    k[0] &= 248u;
    k[31] &= 127u;
    k[31] |= 64u;
    b |= stack_has(k, 16) | stack_has(k + 16, 16);
    mini_printf("no copy of the scalar or the prefix on the stack after signing: %d %d %d", a, rc, b);
    result(a == SHA_OK && rc == SIGN_OK && b == 0);

    mini_printf("[Done] ed25519 %u of %u passed\r\n", (unsigned)s_pass, (unsigned)s_total);
    while (1)
        __asm__ volatile ("wfi");
}
