#!/usr/bin/env python3
"""
sign_image.py: make the signed images used by the signed_update example

Copyright (c) 2026 Armstrong Subero
SPDX-License-Identifier: Apache-2.0

Usage:
  python sign_image.py

Writes images.h beside this script. Needs: pip install cryptography

The two seeds below are demo keys. A real product keeps its signing key off
the device and out of the source tree, and only the public key goes into
the firmware.
"""

import os
import struct

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey

VENDOR_SEED = bytes(range(0x10, 0x30))
OTHER_SEED = bytes(range(0x80, 0xA0))

MAGIC = b"BAOU"


def public_bytes(key):
    return key.public_key().public_bytes(
        serialization.Encoding.Raw, serialization.PublicFormat.Raw)


def make_image(key, version, payload):
    body = MAGIC + struct.pack("<III", version, len(payload), 0) + payload
    return body + key.sign(body)


def c_array(name, data):
    lines = ["static const uint8_t %s[%d] = {" % (name, len(data))]
    for i in range(0, len(data), 12):
        lines.append("    " + " ".join("0x%02x," % b for b in data[i:i + 12]))
    lines.append("};")
    return "\n".join(lines)


def main():
    vendor = Ed25519PrivateKey.from_private_bytes(VENDOR_SEED)
    other = Ed25519PrivateKey.from_private_bytes(OTHER_SEED)

    v1 = make_image(vendor, 1, b"Dabao application, release 1\0")
    v2 = make_image(vendor, 2, b"Dabao application, release 2\0")
    v3_other = make_image(other, 3, b"Dabao application, release 3\0")

    out = [
        "/*",
        " * images.h: signed images for the signed_update example",
        " *",
        " * Made by sign_image.py, do not edit by hand.",
        " */",
        "",
        "#ifndef _SIGNED_UPDATE_IMAGES_H",
        "#define _SIGNED_UPDATE_IMAGES_H",
        "",
        "#include <stdint.h>",
        "",
        c_array("vendor_pub", public_bytes(vendor)),
        "",
        c_array("image_v1", v1),
        "",
        c_array("image_v2", v2),
        "",
        c_array("image_v3_other_key", v3_other),
        "",
        "#endif",
        "",
    ]
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "images.h")
    with open(path, "w", newline="\n") as f:
        f.write("\n".join(out))
    print("wrote", path)


if __name__ == "__main__":
    main()
