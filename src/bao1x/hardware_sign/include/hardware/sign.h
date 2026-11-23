/*
 * Copyright (c) 2026 Armstrong Subero
 * SPDX-License-Identifier: Apache-2.0
 *
 * Signatures for the Baochip-1x: ECDSA on P-256, secp256k1 and P-384 with
 * the RFC 6979 deterministic nonce, and pure Ed25519 (RFC 8032).
 *
 * This module is C on top of the PKE, ALU and SHA drivers and touches no
 * register itself. Point arithmetic runs on the PKE, arithmetic modulo
 * the curve order and the field prime on the PKE's modular functions,
 * wide reductions on the ALU divider, and every hash and HMAC on the SCE
 * hash engine.
 *
 * Byte formats are the usual ones. ECDSA private keys, r, s and each
 * public key coordinate are big endian, ecdsa_key_size(curve) bytes each;
 * a public key is x then y (no 0x04 prefix) and a signature is r then s.
 * Ed25519 seeds, public keys and signatures are the 32, 32 and 64 byte
 * strings of RFC 8032.
 *
 * Secret values (private keys, the expanded Ed25519 key, nonces and their
 * inverses, the RFC 6979 HMAC state) are wiped from this module's stack
 * buffers, and the SCE RAM is cleared, before a signing call returns. The
 * drivers underneath clear the SCE RAM before each PKE call as well. None
 * of this is constant time or hardened against side channels: timing,
 * power and the order of engine calls depend on the key and the nonce.
 *
 * Every PKE call clears the SCE RAM, which also clears the AES key, so call
 * aes_set_key() again after any call here.
 */

#ifndef _HARDWARE_SIGN_H
#define _HARDWARE_SIGN_H

#include <stdint.h>
#include "hardware/pke.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SIGN_OK            0
#define SIGN_ERR_ARG     (-1)   /* NULL pointer, bad length or unsupported curve   */
#define SIGN_ERR_KEY     (-2)   /* private key out of range, or public key invalid */
#define SIGN_ERR_HW      (-3)   /* a PKE, ALU or SHA driver call failed            */
#define SIGN_BAD         (-4)   /* the signature does not verify                   */

/* Hash for the message and for the RFC 6979 HMAC DRBG */
typedef enum {
    SIGN_SHA256 = 0,
    SIGN_SHA512 = 1,
} sign_hash_t;

/* ECDSA. curve is PKE_SECP256R1 (P-256), PKE_SECP256K1 or PKE_SECP384R1. */

/* Bytes in a private key, in r, in s and in each public key coordinate:
 * 32 or 48, or 0 for a curve this module does not support. */
uint32_t ecdsa_key_size(pke_curve_t curve);

/* pub (2 * size bytes) = priv * G. The key must be in 1 to n minus 1. */
int ecdsa_public_key(pke_curve_t curve, const uint8_t *priv, uint8_t *pub);

/* SIGN_OK when both coordinates are below p, the point is on the curve
 * and it is not the all zero encoding of the point at infinity. */
int ecdsa_check_public_key(pke_curve_t curve, const uint8_t *pub);

/* Sign a message hash of 1 to 64 bytes. The nonce is RFC 6979 section 3.2
 * with HMAC over nonce_hash, which should be the hash that made hash. sig
 * is 2 * size bytes. The same key and hash always give the same sig. */
int ecdsa_sign_hash(pke_curve_t curve, const uint8_t *priv, sign_hash_t nonce_hash,
                    const uint8_t *hash, uint32_t hash_len, uint8_t *sig);

/* SIGN_OK when sig is valid for the hash and the public key; SIGN_BAD when
 * r or s is 0 or not below n, or the equation fails; SIGN_ERR_KEY when pub
 * is not a point on the curve. */
int ecdsa_verify_hash(pke_curve_t curve, const uint8_t *pub,
                      const uint8_t *hash, uint32_t hash_len, const uint8_t *sig);

/* The same with the message hashed here by hash (SHA-256 or SHA-512). */
int ecdsa_sign(pke_curve_t curve, const uint8_t *priv, sign_hash_t hash,
               const uint8_t *msg, uint32_t msg_len, uint8_t *sig);
int ecdsa_verify(pke_curve_t curve, const uint8_t *pub, sign_hash_t hash,
                 const uint8_t *msg, uint32_t msg_len, const uint8_t *sig);

/* Ed25519, RFC 8032 section 5.1, pure (no context, no prehash). */

int ed25519_public_key(const uint8_t seed[32], uint8_t pub[32]);

/* The public key is worked out from the seed here, so a wrong one cannot
 * be passed in. */
int ed25519_sign(const uint8_t seed[32], const uint8_t *msg, uint32_t msg_len,
                 uint8_t sig[64]);

/* SIGN_OK when valid; SIGN_BAD when S is not below L, R does not decode
 * or the equation [S]B = R + [k]A fails; SIGN_ERR_KEY when pub does not
 * decode (y not below p, or no x for it). Cofactorless, as RFC 8032 5.1.7
 * allows. */
int ed25519_verify(const uint8_t pub[32], const uint8_t *msg, uint32_t msg_len,
                   const uint8_t sig[64]);

#ifdef __cplusplus
}
#endif

#endif
