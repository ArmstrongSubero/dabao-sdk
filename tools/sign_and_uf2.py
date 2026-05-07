#!/usr/bin/env python3
"""
sign_and_uf2.py v3 - Correct Ed25519ph signing for Baochip-1x

Requirements: pip install pure25519

Signature block layout (768 bytes) from bao1x-api/src/signatures.rs:

  Offset  Size  Field
  0x000   4     JAL instruction (jump over sig block)
  0x004   64    Ed25519ph signature
  0x044   4     aad_len (0 = ed25519ph mode)
  0x048   60    aad (zeros)
  --- UNSIGNED_LEN = 132 bytes (0x84) ---
  --- SealedFields (signed data starts here) ---
  0x084   4     version (0x100)
  0x088   8     magic ("yumyBao3")
  0x090   4     signed_len
  0x094   4     function_code (6 = Baremetal)
  0x098   4     anti_rollback (0)
  0x09C   16    min_semver (zeros)
  0x0AC   16    semver (zeros)
  0x0BC   144   pubkeys (4 x 36 bytes)
  0x14C   20    toolchain (zeros)
  --- SealedFields = 220 bytes ---
  0x160   416   padding (zeros)
  --- Total = 768 bytes ---
  0x300         user code starts

Signature covers SHA-512 prehash of everything from offset 0x84 onward
for signed_len bytes (SealedFields + padding + code).
"""

import sys
import struct
import base64
import hashlib
import binascii

from pure25519.basic import bytes_to_clamped_scalar, scalar_to_bytes, Base

# --- Ed25519ph Implementation ---

def _sha512(m):
    return hashlib.sha512(m).digest()

def _hint(m):
    h = _sha512(m)
    return int(binascii.hexlify(h[::-1]), 16)

def ed25519ph_sign(seed, message):
    """RFC 8032 Ed25519ph with empty context."""
    h = _sha512(seed)
    a_bytes, prefix = h[:32], h[32:]
    a = bytes_to_clamped_scalar(a_bytes)
    A = Base.scalarmult(a)
    pk = A.to_bytes()

    prehash = hashlib.sha512(message).digest()
    dom2 = b"SigEd25519 no Ed25519 collisions" + bytes([1, 0])

    r = _hint(dom2 + prefix + prehash)
    R = Base.scalarmult(r)
    R_bytes = R.to_bytes()
    S = r + _hint(dom2 + R_bytes + pk + prehash) * a
    return R_bytes + scalar_to_bytes(S)

# --- Key ---

DEV_KEY_B64 = "MC4CAQAwBQYDK2VwBCIEIKindlyNoteThisIsADevKeyDontUseForProduction"

DEV_PUB = bytes([
    0x1c, 0x9b, 0xea, 0xe3, 0x2a, 0xea, 0xc8, 0x75,
    0x07, 0xc1, 0x80, 0x94, 0x38, 0x7e, 0xff, 0x1c,
    0x74, 0x61, 0x42, 0x82, 0xaf, 0xfd, 0x81, 0x52,
    0xd8, 0x71, 0x35, 0x2e, 0xdf, 0x3f, 0x58, 0xbb,
])
DEV_TAG = b"dev "

# --- Constants ---

SIGBLOCK_LEN = 768
SIG_VERSION = 0x100
MAGIC_WORD0 = int.from_bytes(b"yumy", 'big')
MAGIC_WORD1 = int.from_bytes(b"Bao3", 'big')
FUNC_BAREMETAL = 6
UNSIGNED_LEN = 132
SEALED_FIELDS_SIZE = 220

def extract_seed(pkcs8_b64):
    der = base64.b64decode(pkcs8_b64)
    return der[16:]

def make_jal(offset):
    assert offset % 2 == 0 and -(1 << 20) <= offset < (1 << 20)
    imm = offset & 0x1FFFFF
    instr = ((imm >> 20) & 1) << 31
    instr |= ((imm >> 1) & 0x3FF) << 21
    instr |= ((imm >> 11) & 1) << 20
    instr |= ((imm >> 12) & 0xFF) << 12
    instr |= 0x6F
    return struct.pack('<I', instr)

def create_signature_block(code_bin):
    seed = extract_seed(DEV_KEY_B64)

    # Verify key
    h = _sha512(seed)
    a = bytes_to_clamped_scalar(h[:32])
    pk = Base.scalarmult(a).to_bytes()
    assert pk == DEV_PUB, "Developer key mismatch!"

    # Build SealedFields
    padding_size = SIGBLOCK_LEN - UNSIGNED_LEN - SEALED_FIELDS_SIZE
    signed_len = SEALED_FIELDS_SIZE + padding_size + len(code_bin)

    sealed = b''
    sealed += struct.pack('<I', SIG_VERSION)
    sealed += struct.pack('<II', MAGIC_WORD0, MAGIC_WORD1)
    sealed += struct.pack('<I', signed_len)
    sealed += struct.pack('<I', FUNC_BAREMETAL)
    sealed += struct.pack('<I', 1)            # anti_rollback
    sealed += bytes(16)                        # min_semver
    sealed += bytes(16)                        # semver
    sealed += bytes(36)                        # pubkey slot 0 (zeros)
    sealed += bytes(36)                        # pubkey slot 1 (zeros)
    sealed += bytes(36)                        # pubkey slot 2 (zeros)
    sealed += DEV_PUB + DEV_TAG                # pubkey slot 3 (developer)
    sealed += bytes(20)                        # toolchain

    assert len(sealed) == SEALED_FIELDS_SIZE

    padding = bytes(padding_size)
    sign_data = sealed + padding + code_bin

    # Ed25519ph sign
    signature = ed25519ph_sign(seed, sign_data)
    assert len(signature) == 64
    print("Signed with developer key (Ed25519ph)")

    # Assemble
    jal = make_jal(SIGBLOCK_LEN)
    block = jal + signature + struct.pack('<I', 0) + bytes(60) + sealed + padding

    assert len(block) == SIGBLOCK_LEN
    return block

def bin_to_uf2(data, base_addr, family_id):
    P = 256
    blocks = []
    n = (len(data) + P - 1) // P
    for i in range(n):
        o = i * P
        chunk = data[o:o+P]
        if len(chunk) < P:
            chunk += bytes(P - len(chunk))
        b = struct.pack('<IIIIIIII',
            0x0A324655, 0x9E5D5157, 0x00002000,
            base_addr + o, P, i, n, family_id)
        b += chunk + bytes(512 - 32 - P - 4)
        b += struct.pack('<I', 0x0AB16F30)
        blocks.append(b)
    return b''.join(blocks)

def main():
    if len(sys.argv) != 5:
        print(f"Usage: {sys.argv[0]} input.bin output.uf2 base_addr family_id")
        sys.exit(1)

    with open(sys.argv[1], 'rb') as f:
        code = f.read()

    print(f"Code: {len(code)} bytes")
    sigblock = create_signature_block(code)
    image = sigblock + code
    print(f"Signed image: {len(image)} bytes")

    uf2 = bin_to_uf2(image, int(sys.argv[3], 0), int(sys.argv[4], 0))
    print(f"UF2: {len(uf2)} bytes ({len(uf2)//512} blocks)")

    with open(sys.argv[2], 'wb') as f:
        f.write(uf2)
    print(f"Written to {sys.argv[2]}")

if __name__ == '__main__':
    main()