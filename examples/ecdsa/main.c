/*
 * main.c: ECDSA with the RFC 6979 nonce on P-256, P-384 and secp256k1 for
 * baoemu
 *
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Drives the SDK's hardware_sign module (ecdsa.c), which runs the points
 * on the PKE, the arithmetic mod n and mod p on the PKE's modular
 * functions and the hashes and the HMAC DRBG on the SCE hash engine.
 *
 * Test vectors:
 *   RFC 6979, "Deterministic Usage of the Digital Signature Algorithm
 *   (DSA) and Elliptic Curve Digital Signature Algorithm (ECDSA)",
 *   appendix A.2.5 (P-256) and A.2.6 (P-384): the private key, the public
 *   key and r and s for the messages "sample" and "test" with SHA-256 and
 *   SHA-512.
 *   secp256k1, which RFC 6979 does not cover: the private key and public
 *   key of the Bitcoin wiki page "Technical background of version 1
 *   Bitcoin addresses" (step 0 and 1), and the RFC 6979 signature of
 *   "Satoshi Nakamoto" with private key 1 and SHA-256 that the
 *   python-ecdsa and trezor-crypto test suites carry, r =
 *   934b1ea1..., s = 2442ce9d... in the low s form Bitcoin uses. Plain
 *   RFC 6979 gives s = n minus that, which is what is checked.
 * tests/sign_ref.py checks every value here from the RFCs on the host.
 *
 * The edge cases are made from the curve itself: with Q = G (private key
 * 1), a hash of 1 and r = n minus 1, s = 1 gives u1 G + u2 Q at infinity;
 * a hash, r and s all equal to x(2G) give u1 = u2 = 1 and a valid
 * signature whose verify has to double a point.
 *
 * Checks, each line ending in PASS or FAIL:
 *   public keys from the private keys;
 *   every vector signed with the exact r and s, verified, and rejected
 *   with one bit flipped in the message, in r and in s;
 *   the published secp256k1 signature verified as given;
 *   the public key of n minus 1 equal to minus G on each curve, where the
 *   PKE ladder meets the point at infinity on its last step, and a
 *   verify with u2 = n minus 1;
 *   r or s of 0 or n, public keys off the curve, at infinity or with x
 *   not below p, the sum at infinity, the doubling case, a zero hash;
 *   private keys of 0 and n, bad arguments;
 *   no copy of the private key or the nonce left on the stack below the
 *   caller after a signature.
 * The time a sign and a verify take on each curve is printed with millis().
 */

#include "bao.h"
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

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n])
        n++;
    return n;
}

/* out = a minus b, big endian, n bytes */
static void sub_be(uint8_t *out, const uint8_t *a, const uint8_t *b, uint32_t n)
{
    int borrow = 0;
    for (uint32_t i = n; i > 0; i--) {
        int d = (int)a[i - 1] - (int)b[i - 1] - borrow;
        borrow = d < 0;
        out[i - 1] = (uint8_t)d;
    }
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

/* The pattern as the drivers hold numbers: 32 bit words, least
 * significant first (only the first 16 bytes are searched) */
static void be_to_le_words(uint8_t *out, const uint8_t *be, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        out[i] = be[n - 1 - i];
}

typedef struct {
    const char   *name;
    pke_curve_t   curve;
    sign_hash_t   hash;
    const char   *msg;
    const char   *r;
    const char   *s;
} vec_t;

/* RFC 6979 A.2.5 */
static const char P256_X[] =
    "C9AFA9D845BA75166B5C215767B1D6934E50C3DB36E89B127B8A622B120F6721";
static const char P256_U[] =
    "60FED4BA255A9D31C961EB74C6356D68C049B8923B61FA6CE669622E60F29FB6"
    "7903FE1008B8BC99A41AE9E95628BC64F2F1B20C2D7E9F5177A3C294D4462299";
/* the nonce A.2.5 gives for SHA-256 and "sample" */
static const char P256_K_SAMPLE[] =
    "A6E3C57DD01ABE90086538398355DD4C3B17AA873382B0F24D6129493D8AAD60";
static const char P256_N[] =
    "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551";
static const char P256_P[] =
    "FFFFFFFF00000001000000000000000000000000FFFFFFFFFFFFFFFFFFFFFFFF";
/* x(2G) on P-256, below n */
static const char P256_2GX[] =
    "7CF27B188D034F7E8A52380304B51AC3C08969E277F21B35A60B48FC47669978";

/* RFC 6979 A.2.6 */
static const char P384_X[] =
    "6B9D3DAD2E1B8C1C05B19875B6659F4DE23C3B667BF297BA9AA47740787137D8"
    "96D5724E4C70A825F872C9EA60D2EDF5";
static const char P384_U[] =
    "EC3A4E415B4E19A4568618029F427FA5DA9A8BC4AE92E02E06AAE5286B300C64"
    "DEF8F0EA9055866064A254515480BC13"
    "8015D9B72D7D57244EA8EF9AC0C621896708A59367F9DFB9F54CA84B3F1C9DB1"
    "288B231C3AE0D4FE7344FD2533264720";

/* Bitcoin wiki, version 1 addresses, steps 0 and 1 */
static const char K256_X[] =
    "18E14A7B6A307F426A94F8114701E7C8E774E7F9A47E2C2035DB29A206321725";
static const char K256_U[] =
    "50863AD64A87AE8A2FE83C1AF1A8403CB53F53E486D8511DAD8A04887E5B2352"
    "2CD470243453A299FA9E77237716103ABC11A1DF38855ED6F2EE187E9C582BA6";
/* p and n of secp256k1 and P-384 (SEC 2), for the n minus 1 keys */
static const char K256_P[] =
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEFFFFFC2F";
static const char P384_P[] =
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFE"
    "FFFFFFFF0000000000000000FFFFFFFF";
static const char P384_N[] =
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFC7634D81F4372DDF"
    "581A0DB248B0A77AECEC196ACCC52973";
static const char K256_N[] =
    "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFEBAAEDCE6AF48A03BBFD25E8CD0364141";
/* "Satoshi Nakamoto", private key 1, SHA-256, low s form */
static const char K256_SN_R[] =
    "934B1EA10A4B3C1757E2B0C017D0B6143CE3C9A7E6A4A49860D7A6AB210EE3D8";
static const char K256_SN_S[] =
    "2442CE9D2B916064108014783E923EC36B49743E2FFA1C4496F01A512AAFD9E5";

static const vec_t s_vec[] = {
    { "P-256 SHA-256 sample", PKE_SECP256R1, SIGN_SHA256, "sample",
      "EFD48B2AACB6A8FD1140DD9CD45E81D69D2C877B56AAF991C34D0EA84EAF3716",
      "F7CB1C942D657C41D436C7A1B6E29F65F3E900DBB9AFF4064DC4AB2F843ACDA8" },
    { "P-256 SHA-512 sample", PKE_SECP256R1, SIGN_SHA512, "sample",
      "8496A60B5E9B47C825488827E0495B0E3FA109EC4568FD3F8D1097678EB97F00",
      "2362AB1ADBE2B8ADF9CB9EDAB740EA6049C028114F2460F96554F61FAE3302FE" },
    { "P-256 SHA-256 test", PKE_SECP256R1, SIGN_SHA256, "test",
      "F1ABB023518351CD71D881567B1EA663ED3EFCF6C5132B354F28D3B0B7D38367",
      "019F4113742A2B14BD25926B49C649155F267E60D3814B4C0CC84250E46F0083" },
    { "P-256 SHA-512 test", PKE_SECP256R1, SIGN_SHA512, "test",
      "461D93F31B6540894788FD206C07CFA0CC35F46FA3C91816FFF1040AD1581A04",
      "39AF9F15DE0DB8D97E72719C74820D304CE5226E32DEDAE67519E840D1194E55" },
    { "P-384 SHA-256 sample", PKE_SECP384R1, SIGN_SHA256, "sample",
      "21B13D1E013C7FA1392D03C5F99AF8B30C570C6F98D4EA8E354B63A21D3DAA33"
      "BDE1E888E63355D92FA2B3C36D8FB2CD",
      "F3AA443FB107745BF4BD77CB3891674632068A10CA67E3D45DB2266FA7D1FEEB"
      "EFDC63ECCD1AC42EC0CB8668A4FA0AB0" },
    { "P-384 SHA-512 sample", PKE_SECP384R1, SIGN_SHA512, "sample",
      "ED0959D5880AB2D869AE7F6C2915C6D60F96507F9CB3E047C0046861DA4A799C"
      "FE30F35CC900056D7C99CD7882433709",
      "512C8CCEEE3890A84058CE1E22DBC2198F42323CE8ACA9135329F03C068E5112"
      "DC7CC3EF3446DEFCEB01A45C2667FDD5" },
    { "P-384 SHA-256 test", PKE_SECP384R1, SIGN_SHA256, "test",
      "6D6DEFAC9AB64DABAFE36C6BF510352A4CC27001263638E5B16D9BB51D451559"
      "F918EEDAF2293BE5B475CC8F0188636B",
      "2D46F3BECBCC523D5F1A1256BF0C9B024D879BA9E838144C8BA6BAEB4B53B47D"
      "51AB373F9845C0514EEFB14024787265" },
    { "P-384 SHA-512 test", PKE_SECP384R1, SIGN_SHA512, "test",
      "A0D5D090C9980FAF3C2CE57B7AE951D31977DD11C775D314AF55F76C676447D0"
      "6FB6495CD21B4B6E340FC236584FB277",
      "976984E59B4C77B0E8E4460DCA3D9F20E07B9BB1F63BEEFAF576F6B2E8B22463"
      "4A2092CD3792E0159AD9CEE37659C736" },
};

static const char *curve_name(pke_curve_t c)
{
    return c == PKE_SECP256R1 ? "P-256" : c == PKE_SECP384R1 ? "P-384" : "secp256k1";
}

static void check_pub(pke_curve_t c, const char *x_hex, const char *u_hex)
{
    uint8_t x[48], u[96], pub[96];
    uint32_t n = ecdsa_key_size(c);
    int rc;

    unhex(x_hex, x);
    unhex(u_hex, u);
    rc = ecdsa_public_key(c, x, pub);
    mini_printf("%s public key: ", curve_name(c));
    hex(pub, 4);
    mini_printf("...");
    hex(pub + 2u * n - 4u, 4);
    result(rc == SIGN_OK && same(pub, u, 2u * n));
    rc = ecdsa_check_public_key(c, u);
    mini_printf("%s public key on the curve: %d", curve_name(c), rc);
    result(rc == SIGN_OK);
}

static void run_vector(const vec_t *v)
{
    uint8_t x[48], u[96], r[48], s[48], sig[96], m[8];
    uint32_t n = ecdsa_key_size(v->curve), ml = slen(v->msg);
    int rc, a, b, c;

    unhex(v->curve == PKE_SECP256R1 ? P256_X : P384_X, x);
    unhex(v->curve == PKE_SECP256R1 ? P256_U : P384_U, u);
    unhex(v->r, r);
    unhex(v->s, s);
    for (uint32_t i = 0; i < ml; i++)
        m[i] = (uint8_t)v->msg[i];

    rc = ecdsa_sign(v->curve, x, v->hash, m, ml, sig);
    mini_printf("%s sign: r ", v->name);
    hex(sig, n);
    mini_printf(" s ");
    hex(sig + n, n);
    result(rc == SIGN_OK && same(sig, r, n) && same(sig + n, s, n));

    rc = ecdsa_verify(v->curve, u, v->hash, m, ml, sig);
    mini_printf("%s verify: %d", v->name, rc);
    result(rc == SIGN_OK);

    m[0] ^= 1u;
    a = ecdsa_verify(v->curve, u, v->hash, m, ml, sig);
    m[0] ^= 1u;
    sig[n - 1u] ^= 1u;
    b = ecdsa_verify(v->curve, u, v->hash, m, ml, sig);
    sig[n - 1u] ^= 1u;
    sig[2u * n - 1u] ^= 1u;
    c = ecdsa_verify(v->curve, u, v->hash, m, ml, sig);
    sig[2u * n - 1u] ^= 1u;
    mini_printf("%s one bit flipped in the message, r, s: %d %d %d", v->name, a, b, c);
    result(a == SIGN_BAD && b == SIGN_BAD && c == SIGN_BAD);
}

static void timing(pke_curve_t c, const char *x_hex, const char *u_hex)
{
    uint8_t x[48], u[96], sig[96];
    static const uint8_t msg[] = "timing";
    uint64_t t0, t1, t2;
    int a, b;

    unhex(x_hex, x);
    unhex(u_hex, u);
    t0 = millis();
    a = ecdsa_sign(c, x, SIGN_SHA256, msg, 6, sig);
    t1 = millis();
    b = ecdsa_verify(c, u, SIGN_SHA256, msg, 6, sig);
    t2 = millis();
    mini_printf("%s time: sign %u ms, verify %u ms\r\n", curve_name(c),
                (unsigned)(t1 - t0), (unsigned)(t2 - t1));
    mini_printf("%s timed sign and verify: %d %d", curve_name(c), a, b);
    result(a == SIGN_OK && b == SIGN_OK);
}

int main(void)
{
    uint8_t x[48], g[96], u[96], h[32], sig[96], sig2[96], t[48], nn[48];
    uint8_t pat[32];
    static const uint8_t sn[] = "Satoshi Nakamoto";
    int rc, a, b, c, d;

    bao_init();
    mini_printf("\r\necdsa start\r\n");

    mini_printf("key sizes: %u %u %u %u",
                (unsigned)ecdsa_key_size(PKE_SECP256R1), (unsigned)ecdsa_key_size(PKE_SECP256K1),
                (unsigned)ecdsa_key_size(PKE_SECP384R1), (unsigned)ecdsa_key_size(PKE_SECP192R1));
    result(ecdsa_key_size(PKE_SECP256R1) == 32 && ecdsa_key_size(PKE_SECP256K1) == 32 &&
           ecdsa_key_size(PKE_SECP384R1) == 48 && ecdsa_key_size(PKE_SECP192R1) == 0);

    check_pub(PKE_SECP256R1, P256_X, P256_U);
    check_pub(PKE_SECP384R1, P384_X, P384_U);
    check_pub(PKE_SECP256K1, K256_X, K256_U);

    for (uint32_t i = 0; i < sizeof(s_vec) / sizeof(s_vec[0]); i++)
        run_vector(&s_vec[i]);

    /* secp256k1, private key 1, "Satoshi Nakamoto" */
    for (uint32_t i = 0; i < 32; i++)
        x[i] = 0;
    x[31] = 1;
    rc = ecdsa_public_key(PKE_SECP256K1, x, g);
    unhex(K256_N, nn);
    a = ecdsa_sign(PKE_SECP256K1, x, SIGN_SHA256, sn, 16, sig);
    sub_be(t, nn, sig + 32, 32);
    unhex(K256_SN_R, sig2);
    unhex(K256_SN_S, sig2 + 32);
    mini_printf("secp256k1 Satoshi Nakamoto sign: r ");
    hex(sig, 32);
    mini_printf(" n minus s ");
    hex(t, 32);
    result(rc == SIGN_OK && a == SIGN_OK && same(sig, sig2, 32) && same(t, sig2 + 32, 32));
    a = ecdsa_verify(PKE_SECP256K1, g, SIGN_SHA256, sn, 16, sig);
    b = ecdsa_verify(PKE_SECP256K1, g, SIGN_SHA256, sn, 16, sig2);
    mini_printf("secp256k1 Satoshi Nakamoto verify, s and low s as published: %d %d", a, b);
    result(a == SIGN_OK && b == SIGN_OK);
    sig2[0] ^= 0x80u;
    a = ecdsa_verify(PKE_SECP256K1, g, SIGN_SHA256, sn, 15, sig);
    b = ecdsa_verify(PKE_SECP256K1, g, SIGN_SHA256, sn, 16, sig2);
    sig2[0] ^= 0x80u;
    sig2[63] ^= 1u;
    c = ecdsa_verify(PKE_SECP256K1, g, SIGN_SHA256, sn, 16, sig2);
    mini_printf("secp256k1 shorter message, flipped r, flipped s: %d %d %d", a, b, c);
    result(a == SIGN_BAD && b == SIGN_BAD && c == SIGN_BAD);

    /* P-256 edge cases. u is the A.2.5 public key, sig2 a good signature. */
    unhex(P256_X, x);
    unhex(P256_U, u);
    unhex(P256_N, nn);
    for (uint32_t i = 0; i < 32; i++)
        h[i] = (uint8_t)(i + 1u);
    rc = ecdsa_sign_hash(PKE_SECP256R1, x, SIGN_SHA256, h, 32, sig2);
    a = ecdsa_verify_hash(PKE_SECP256R1, u, h, 32, sig2);
    mini_printf("P-256 sign_hash and verify_hash: %d %d", rc, a);
    result(rc == SIGN_OK && a == SIGN_OK);

    for (uint32_t i = 0; i < 64; i++)
        sig[i] = sig2[i];
    for (uint32_t i = 0; i < 32; i++)
        sig[i] = 0;
    a = ecdsa_verify_hash(PKE_SECP256R1, u, h, 32, sig);
    for (uint32_t i = 0; i < 32; i++)
        sig[i] = nn[i];
    b = ecdsa_verify_hash(PKE_SECP256R1, u, h, 32, sig);
    for (uint32_t i = 0; i < 32; i++) {
        sig[i] = sig2[i];
        sig[32 + i] = 0;
    }
    c = ecdsa_verify_hash(PKE_SECP256R1, u, h, 32, sig);
    for (uint32_t i = 0; i < 32; i++)
        sig[32 + i] = nn[i];
    d = ecdsa_verify_hash(PKE_SECP256R1, u, h, 32, sig);
    mini_printf("r of 0 and n, s of 0 and n: %d %d %d %d", a, b, c, d);
    result(a == SIGN_BAD && b == SIGN_BAD && c == SIGN_BAD && d == SIGN_BAD);

    u[63] ^= 1u;
    a = ecdsa_check_public_key(PKE_SECP256R1, u);
    b = ecdsa_verify_hash(PKE_SECP256R1, u, h, 32, sig2);
    u[63] ^= 1u;
    mini_printf("public key off the curve: %d %d", a, b);
    result(a == SIGN_ERR_KEY && b == SIGN_ERR_KEY);

    for (uint32_t i = 0; i < 64; i++)
        g[i] = 0;
    a = ecdsa_check_public_key(PKE_SECP256R1, g);
    b = ecdsa_verify_hash(PKE_SECP256R1, g, h, 32, sig2);
    unhex(P256_P, g);
    for (uint32_t i = 0; i < 32; i++)
        g[32 + i] = u[32 + i];
    c = ecdsa_check_public_key(PKE_SECP256R1, g);
    mini_printf("public key at infinity, x of p: %d %d %d", a, b, c);
    result(a == SIGN_ERR_KEY && b == SIGN_ERR_KEY && c == SIGN_ERR_KEY);

    /* Q = G from private key 1 */
    for (uint32_t i = 0; i < 32; i++)
        t[i] = 0;
    t[31] = 1;
    rc = ecdsa_public_key(PKE_SECP256R1, t, g);
    for (uint32_t i = 0; i < 32; i++)
        h[i] = 0;
    h[31] = 1;
    sub_be(sig, nn, h, 32);             /* r = n minus 1 */
    for (uint32_t i = 0; i < 32; i++)
        sig[32 + i] = h[i];             /* s = 1 */
    a = ecdsa_verify_hash(PKE_SECP256R1, g, h, 32, sig);
    mini_printf("u1 G + u2 Q at infinity: %d", a);
    result(rc == SIGN_OK && a == SIGN_BAD);

    unhex(P256_2GX, h);
    for (uint32_t i = 0; i < 32; i++) {
        sig[i] = h[i];
        sig[32 + i] = h[i];
    }
    a = ecdsa_verify_hash(PKE_SECP256R1, g, h, 32, sig);
    sig[31] ^= 1u;
    b = ecdsa_verify_hash(PKE_SECP256R1, g, h, 32, sig);
    mini_printf("u1 G equal to u2 Q, so verify doubles: %d, r changed %d", a, b);
    result(a == SIGN_OK && b == SIGN_BAD);

    for (uint32_t i = 0; i < 32; i++)
        h[i] = 0;
    rc = ecdsa_sign_hash(PKE_SECP256R1, x, SIGN_SHA256, h, 32, sig);
    a = ecdsa_verify_hash(PKE_SECP256R1, u, h, 32, sig);
    mini_printf("hash of 0 (u1 = 0): %d %d", rc, a);
    result(rc == SIGN_OK && a == SIGN_OK);

    for (uint32_t i = 0; i < 32; i++)
        t[i] = 0;
    a = ecdsa_sign_hash(PKE_SECP256R1, t, SIGN_SHA256, h, 32, sig);
    b = ecdsa_sign_hash(PKE_SECP256R1, nn, SIGN_SHA256, h, 32, sig);
    c = ecdsa_public_key(PKE_SECP256R1, nn, g);
    mini_printf("private key 0 and n: %d %d %d", a, b, c);
    result(a == SIGN_ERR_KEY && b == SIGN_ERR_KEY && c == SIGN_ERR_KEY);

    /* (n minus 1) G = minus G: the PKE ladder's other point reaches nG on
     * its last step. x must equal Gx and y + Gy must equal p. */
    {
        static const pke_curve_t cv[3] = { PKE_SECP256R1, PKE_SECP256K1, PKE_SECP384R1 };
        static const char *const ns[3] = { P256_N, K256_N, P384_N };
        static const char *const ps[3] = { P256_P, K256_P, P384_P };
        uint8_t one[48], nm1[48], g1[96], g2[96], pp[48], sum[48];
        int r1[3], ok = 1;
        for (uint32_t j = 0; j < 3; j++) {
            uint32_t z = ecdsa_key_size(cv[j]);
            for (uint32_t i = 0; i < z; i++)
                one[i] = 0;
            one[z - 1] = 1;
            unhex(ns[j], nn);
            unhex(ps[j], pp);
            sub_be(nm1, nn, one, z);
            r1[j] = ecdsa_public_key(cv[j], one, g1);
            r1[j] |= ecdsa_public_key(cv[j], nm1, g2);
            {
                uint32_t cy = 0;
                for (uint32_t i = z; i > 0; i--) {
                    uint32_t t2 = (uint32_t)g1[z + i - 1] + g2[z + i - 1] + cy;
                    sum[i - 1] = (uint8_t)t2;
                    cy = t2 >> 8;
                }
            }
            ok &= r1[j] == SIGN_OK && same(g1, g2, z) && same(sum, pp, z);
        }
        mini_printf("public key of n minus 1 is minus G on all three curves: %d %d %d",
                    r1[0], r1[1], r1[2]);
        result(ok);
        /* s = n minus r makes u2 = n minus 1 */
        unhex(P256_N, nn);
        for (uint32_t i = 0; i < 32; i++)
            sig[i] = sig2[i];
        sub_be(sig + 32, nn, sig2, 32);
        a = ecdsa_verify_hash(PKE_SECP256R1, u, h, 32, sig);
        mini_printf("s = n minus r (u2 = n minus 1): %d", a);
        result(a == SIGN_BAD);
    }

    a = ecdsa_sign_hash(PKE_SECP192R1, x, SIGN_SHA256, h, 32, sig);
    b = ecdsa_sign_hash(PKE_SECP256R1, NULL, SIGN_SHA256, h, 32, sig);
    c = ecdsa_verify_hash(PKE_SECP256R1, u, h, 0, sig2);
    d = ecdsa_verify_hash(PKE_SECP256R1, u, h, 65, sig2);
    rc = ecdsa_verify(PKE_SECP256R1, u, SIGN_SHA256, NULL, 1, sig2);
    mini_printf("unsupported curve, NULL key, hash of 0 and 65 bytes, NULL message: %d %d %d %d %d",
                a, b, c, d, rc);
    result(a == SIGN_ERR_ARG && b == SIGN_ERR_ARG && c == SIGN_ERR_ARG && d == SIGN_ERR_ARG &&
           rc == SIGN_ERR_ARG);

    timing(PKE_SECP256R1, P256_X, P256_U);
    timing(PKE_SECP256K1, K256_X, K256_U);
    timing(PKE_SECP384R1, P384_X, P384_U);

    /* Last: the A.2.5 signature of "sample", then look for the key and the
     * nonce below this frame. The SCE RAM is left cleared for the host to
     * check. */
    rc = ecdsa_sign(PKE_SECP256R1, x, SIGN_SHA256, (const uint8_t *)"sample", 6, sig);
    a = 0;
    unhex(P256_K_SAMPLE, t);
    be_to_le_words(pat, t, 32);
    a |= stack_has(t, 16) | stack_has(pat, 16) | stack_has(t + 16, 16) | stack_has(pat + 16, 16);
    be_to_le_words(pat, x, 32);
    a |= stack_has(x, 16) | stack_has(pat, 16) | stack_has(x + 16, 16) | stack_has(pat + 16, 16);
    mini_printf("no copy of the key or the nonce on the stack after signing: %d %d", rc, a);
    result(rc == SIGN_OK && a == 0);

    mini_printf("[Done] ecdsa %u of %u passed\r\n", (unsigned)s_pass, (unsigned)s_total);
    while (1) {
    }
    return 0;
}
