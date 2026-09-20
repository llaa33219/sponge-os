#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
#
# gen_png.py — generate a small solid-colour PNG for W6 staging.
#
# Produces a 32x32 24-bit RGB PNG filled with the given colour.
# Used by run/sponge-de-bgmenu.run (and bgimage, bgimage-badpath)
# to stage /system/background/default.png at scenario build time
# without depending on ImageMagick or PIL — the build env must
# work in a fresh checkout where neither is guaranteed installed.
#
# Args: gen_png.py <out_path> <r> <g> <b>
#
# The PNG bytes are written with stdlib only (zlib + struct).

import struct
import sys
import zlib


def png_chunk(chunk_type: bytes, data: bytes) -> bytes:
    """One PNG chunk: length + type + data + CRC32."""
    length = struct.pack(">I", len(data))
    crc = struct.pack(">I", zlib.crc32(chunk_type + data) & 0xFFFFFFFF)
    return length + chunk_type + data + crc


def make_png(width: int, height: int, r: int, g: int, b: int) -> bytes:
    """Build a width*height 24-bit RGB PNG filled with (r,g,b)."""
    signature = b"\x89PNG\r\n\x1a\n"

    ihdr_data = struct.pack(">IIBBBBB", width, height,
                            8,  # bit depth
                            2,  # colour type 2 = truecolour RGB
                            0,  # compression
                            0,  # filter
                            0)  # interlace
    ihdr = png_chunk(b"IHDR", ihdr_data)

    row = bytes([0]) + bytes([r, g, b] * width)  # filter byte 0 + RGB
    raw = row * height
    idat = png_chunk(b"IDAT", zlib.compress(raw, 9))

    iend = png_chunk(b"IEND", b"")

    return signature + ihdr + idat + iend


def main() -> int:
    if len(sys.argv) != 5:
        print("usage: gen_png.py <out_path> <r> <g> <b>", file=sys.stderr)
        return 1

    out_path = sys.argv[1]
    r = int(sys.argv[2])
    g = int(sys.argv[3])
    b = int(sys.argv[4])

    if not (0 <= r <= 255 and 0 <= g <= 255 and 0 <= b <= 255):
        print("RGB values must be 0..255", file=sys.stderr)
        return 1

    png = make_png(32, 32, r, g, b)

    with open(out_path, "wb") as f:
        f.write(png)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())