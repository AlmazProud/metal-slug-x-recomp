"""Lectura y escritura mínima de imágenes RGB (BMP de 24 bits y PNG de 8 bits por canal), sin
dependencias externas. La usan las herramientas de comparación contra MAME.

Una imagen es (ancho, alto, bytes RGB por filas de arriba abajo).
"""
from __future__ import annotations

import struct
import zlib
from pathlib import Path

Imagen = tuple[int, int, bytes]


def leer_bmp(ruta: Path) -> Imagen:
    d = Path(ruta).read_bytes()
    if d[:2] != b"BM":
        raise ValueError(f"{ruta}: no es BMP")
    inicio = struct.unpack_from("<I", d, 10)[0]
    ancho, alto = struct.unpack_from("<ii", d, 18)
    bpp = struct.unpack_from("<H", d, 28)[0]
    if bpp != 24:
        raise ValueError(f"{ruta}: BMP de {bpp} bits")
    fila = (ancho * 3 + 3) & ~3
    abajo_arriba = alto > 0
    alto = abs(alto)
    out = bytearray()
    for y in range(alto):
        yy = alto - 1 - y if abajo_arriba else y
        f = d[inicio + yy * fila: inicio + yy * fila + ancho * 3]
        for x in range(ancho):
            b, g, r = f[3 * x], f[3 * x + 1], f[3 * x + 2]
            out += bytes((r, g, b))
    return ancho, alto, bytes(out)


def _paeth(a: int, b: int, c: int) -> int:
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def leer_png(ruta: Path) -> Imagen:
    d = Path(ruta).read_bytes()
    if d[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"{ruta}: no es PNG")
    pos, idat, ancho, alto, tipo = 8, b"", 0, 0, 0
    while pos < len(d):
        n, t = struct.unpack_from(">I4s", d, pos)
        cuerpo = d[pos + 8: pos + 8 + n]
        if t == b"IHDR":
            ancho, alto, prof, tipo, _, _, entrelazado = struct.unpack(">IIBBBBB", cuerpo)
            if prof != 8 or tipo not in (2, 6) or entrelazado:
                raise ValueError(f"{ruta}: PNG no soportado (profundidad {prof}, tipo {tipo})")
        elif t == b"IDAT":
            idat += cuerpo
        pos += 12 + n
    canales = 3 if tipo == 2 else 4
    crudo = zlib.decompress(idat)
    paso = ancho * canales
    out = bytearray()
    previa = bytearray(paso)
    i = 0
    for _ in range(alto):
        filtro = crudo[i]
        f = bytearray(crudo[i + 1: i + 1 + paso])
        i += 1 + paso
        for x in range(paso):
            a = f[x - canales] if x >= canales else 0
            b = previa[x]
            c = previa[x - canales] if x >= canales else 0
            if filtro == 1:
                f[x] = (f[x] + a) & 0xFF
            elif filtro == 2:
                f[x] = (f[x] + b) & 0xFF
            elif filtro == 3:
                f[x] = (f[x] + ((a + b) >> 1)) & 0xFF
            elif filtro == 4:
                f[x] = (f[x] + _paeth(a, b, c)) & 0xFF
        previa = f
        if canales == 3:
            out += f
        else:
            for x in range(ancho):
                out += f[4 * x: 4 * x + 3]
    return ancho, alto, bytes(out)


def leer(ruta: Path) -> Imagen:
    return leer_png(ruta) if str(ruta).lower().endswith(".png") else leer_bmp(ruta)


def escribir_png(ruta: Path, img: Imagen) -> None:
    ancho, alto, rgb = img

    def bloque(t: bytes, cuerpo: bytes) -> bytes:
        return struct.pack(">I", len(cuerpo)) + t + cuerpo + struct.pack(">I", zlib.crc32(t + cuerpo) & 0xFFFFFFFF)

    crudo = b"".join(b"\0" + rgb[y * ancho * 3:(y + 1) * ancho * 3] for y in range(alto))
    Path(ruta).write_bytes(b"\x89PNG\r\n\x1a\n" + bloque(b"IHDR", struct.pack(">IIBBBBB", ancho, alto, 8, 2, 0, 0, 0))
                           + bloque(b"IDAT", zlib.compress(crudo, 6)) + bloque(b"IEND", b""))


def escalar(img: Imagen, k: int) -> Imagen:
    ancho, alto, rgb = img
    out = bytearray()
    for y in range(alto):
        fila = bytearray()
        for x in range(ancho):
            fila += rgb[3 * (y * ancho + x): 3 * (y * ancho + x) + 3] * k
        out += bytes(fila) * k
    return ancho * k, alto * k, bytes(out)
