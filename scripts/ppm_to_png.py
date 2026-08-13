#!/usr/bin/env python3
"""Convert an 8-bit binary RGB PPM to PNG using only the Python standard library."""

from __future__ import annotations

import argparse
import binascii
import struct
import zlib
from pathlib import Path


def _token(data: bytes, position: int) -> tuple[bytes, int]:
    while True:
        while position < len(data) and data[position] in b" \t\r\n":
            position += 1
        if position < len(data) and data[position] == ord("#"):
            newline = data.find(b"\n", position)
            if newline < 0:
                raise ValueError("unterminated PPM comment")
            position = newline + 1
            continue
        break
    end = position
    while end < len(data) and data[end] not in b" \t\r\n#":
        end += 1
    if end == position:
        raise ValueError("missing PPM header token")
    return data[position:end], end


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    position = 0
    magic, position = _token(data, position)
    width_token, position = _token(data, position)
    height_token, position = _token(data, position)
    maximum_token, position = _token(data, position)
    if magic != b"P6":
        raise ValueError("only binary RGB PPM (P6) is supported")
    width = int(width_token)
    height = int(height_token)
    maximum = int(maximum_token)
    if width <= 0 or height <= 0 or maximum != 255:
        raise ValueError("PPM must have positive dimensions and max value 255")
    if position >= len(data) or data[position] not in b" \t\r\n":
        raise ValueError("PPM header is not terminated by whitespace")
    if data[position : position + 2] == b"\r\n":
        position += 2
    else:
        position += 1
    pixels = data[position:]
    expected = width * height * 3
    if len(pixels) != expected:
        raise ValueError(f"expected {expected} RGB bytes, found {len(pixels)}")
    return width, height, pixels


def _chunk(kind: bytes, payload: bytes) -> bytes:
    checksum = binascii.crc32(kind + payload) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", checksum)


def write_png(path: Path, width: int, height: int, pixels: bytes) -> None:
    stride = width * 3
    scanlines = b"".join(
        b"\x00" + pixels[row * stride : (row + 1) * stride]
        for row in range(height)
    )
    header = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    output = (
        b"\x89PNG\r\n\x1a\n"
        + _chunk(b"IHDR", header)
        + _chunk(b"IDAT", zlib.compress(scanlines, level=9))
        + _chunk(b"IEND", b"")
    )
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    width, height, pixels = read_ppm(args.input)
    write_png(args.output, width, height, pixels)
    print(f"wrote {args.output} ({width}x{height})")


if __name__ == "__main__":
    main()
