"""Minimal reading and writing of RGB images (24-bit BMP and 8-bit-per-channel PNG), with no
external dependencies. Used by the comparison tools against MAME.

An image is (width, height, RGB bytes in rows from top to bottom).
"""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

Image = tuple[int, int, bytes]


def read_bmp(path: Path) -> Image:
    d = Path(path).read_bytes()
    if d[:2] != b"BM":
        raise ValueError(f"{path}: not a BMP")
    start = struct.unpack_from("<I", d, 10)[0]
    width, height = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 24:
        raise ValueError(f"{path}: {bpp}-bit BMP")
    row = (width * 3 + 3) & ~3
    bottom_up = height > 0
    height = abs(height)
    out = bytearray()
    for y in range(height):
        yy = height - 1 - y if bottom_up else y
        f = d[start + yy * row: start + yy * row + width * 3]
        for x in range(width):
            b, g, r = f[3 * x], f[3 * x + 1], f[3 * x + 2]
            out += bytes((r, g, b))
    return width, height, bytes(out)


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def read_png(path: Path) -> Image:
    d = Path(path).read_bytes()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{path}: not a PNG")
    pos, idat, width, height, color_type = 8, b"", 0, 0, 0
    while pos < len(d):
        n, t = struct.unpack_from(">I4s", d, pos)
        body = d[pos + 8: pos + 8 + n]
        if t == b"IHDR":
            width, height, depth, color_type, _, _, interlaced = struct.unpack(">IIBBBBB", body)
            if depth != 8 or color_type not in (2, 6) or interlaced:
                raise ValueError(f"{path}: unsupported PNG (depth {depth}, type {color_type})")
        elif t == b"IDAT":
            idat += body
        pos += 12 + n
    channels = 3 if color_type == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * channels
    out = bytearray()
    prev = bytearray(stride)
    i = 0
    for _ in range(height):
        filt = raw[i]
        f = bytearray(raw[i + 1: i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = f[x - channels] if x >= channels else 0
            b = prev[x]
            c = prev[x - channels] if x >= channels else 0
            if filt == 1:
                f[x] = (f[x] + a) & 0xFF
            elif filt == 2:
                f[x] = (f[x] + b) & 0xFF
            elif filt == 3:
                f[x] = (f[x] + ((a + b) >> 1)) & 0xFF
            elif filt == 4:
                f[x] = (f[x] + _paeth(a, b, c)) & 0xFF
        prev = f
        if channels == 3:
            out += f
        else:
            for x in range(width):
                out += f[4 * x: 4 * x + 3]
    return width, height, bytes(out)


def read(path: Path) -> Image:
    return read_png(path) if str(path).lower().endswith(".png") else read_bmp(path)


def write_png(path: Path, img: Image) -> None:
    width, height, rgb = img

    def chunk(t: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + t + body + struct.pack(">I", zlib.crc32(t + body) & 0xFFFFFFFF)

    raw = b"".join(b"\0" + rgb[y * width * 3:(y + 1) * width * 3] for y in range(height))
    Path(path).write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


def scale(img: Image, k: int) -> Image:
    width, height, rgb = img
    out = bytearray()
    for y in range(height):
        row = bytearray()
        for x in range(width):
            row += rgb[3 * (y * width + x): 3 * (y * width + x) + 3] * k
        out += bytes(row) * k
    return width * k, height * k, bytes(out)
