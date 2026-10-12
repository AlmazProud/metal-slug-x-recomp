"""Compara capturas del port (port_NNNNNN.bmp) con las de MAME (mame_NNNNNN.png) cuadro a cuadro.

Uso:
  python comparar_capturas.py --port CARPETA --mame CARPETA [--salida CARPETA]

Para cada cuadro presente en ambas carpetas informa cuántos píxeles difieren y, con --salida,
escribe una imagen MAME | port | diferencias (en rojo) para revisarla.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import imagenes  # noqa: E402


def cuadros(carpeta: Path, prefijo: str, ext: str) -> dict[int, Path]:
    r = {}
    for f in carpeta.glob(f"{prefijo}_*.{ext}"):
        m = re.fullmatch(rf"{prefijo}_(\d+)\.{ext}", f.name)
        if m:
            r[int(m.group(1))] = f
    return r


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=Path, required=True)
    ap.add_argument("--mame", type=Path, required=True)
    ap.add_argument("--salida", type=Path)
    a = ap.parse_args()

    p = cuadros(a.port, "port", "bmp")
    m = cuadros(a.mame, "mame", "png")
    comunes = sorted(set(p) & set(m))
    if not comunes:
        print("No hay cuadros en común")
        return 1
    if a.salida:
        a.salida.mkdir(parents=True, exist_ok=True)
    total_ok = 0
    for n in comunes:
        wp, hp, rp = imagenes.leer(p[n])
        wm, hm, rm = imagenes.leer(m[n])
        if (wp, hp) != (wm, hm):
            print(f"cuadro {n}: tamaños distintos port {wp}x{hp}, MAME {wm}x{hm}")
            continue
        distintos = sum(1 for i in range(0, len(rp), 3) if rp[i:i + 3] != rm[i:i + 3])
        estado = "IDENTICO" if distintos == 0 else f"{distintos} píxeles distintos ({100 * distintos / (wp * hp):.2f}%)"
        print(f"cuadro {n:6d}: {estado}")
        total_ok += distintos == 0
        if a.salida and distintos:
            diff = bytearray()
            for i in range(0, len(rp), 3):
                diff += b"\xff\x00\x00" if rp[i:i + 3] != rm[i:i + 3] else bytes(v // 4 for v in rm[i:i + 3])
            filas = bytearray()
            for y in range(hp):
                s = slice(y * wp * 3, (y + 1) * wp * 3)
                filas += rm[s] + rp[s] + diff[s]
            imagenes.escribir_png(a.salida / f"cmp_{n:06d}.png", imagenes.escalar((wp * 3, hp, bytes(filas)), 2))
    print(f"idénticos: {total_ok}/{len(comunes)}")
    return 0 if total_ok == len(comunes) else 1


if __name__ == "__main__":
    sys.exit(main())
