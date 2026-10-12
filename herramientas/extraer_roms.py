"""Reconstruye el set de MAME (mslugx + neogeo) a partir de los archivos de DotEmu/GOG.

Uso:
    python herramientas/extraer_roms.py [--roms DIR] [--salida DIR] [--mame EXE]

- Lee los archivos mslugx_* de --roms (por defecto %PD_ROMS%).
- Escribe <salida>/mslugx/*.{p1,ep1,s1,m1,v*,c*} y <salida>/neogeo/* (por defecto <roms>/mame).
- Obtiene los nombres y hashes esperados de `mame -listxml` (MAME es el oráculo),
  verifica CRC32 y SHA1 de cada archivo reconstruido y muestra un reporte.

Los formatos y transformaciones están documentados en docs/ROM_MAP.md.
Este script no contiene datos de los ROMs: solo transformaciones.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
import zlib
from pathlib import Path

import numpy as np

MB = 1 << 20


# ---------------------------------------------------------------- transformaciones

def fix_dotemu_a_neogeo(d: bytes) -> bytes:
    """Fix 8x8 de DotEmu (4bpp, fila mayor, 4 bytes por fila, nibble bajo = píxel izquierdo)
    al formato S de la Neo Geo (por tile de 32 bytes: 4 grupos de columnas de 8 bytes,
    un byte por fila; grupos en el orden de píxeles 4-5, 6-7, 0-1, 2-3)."""
    t = np.frombuffer(d, np.uint8).reshape(-1, 8, 4)        # tile, fila, par de píxeles
    grupo_de_par = (2, 3, 0, 1)                             # par k -> grupo de 8 bytes en el tile S
    out = np.empty((t.shape[0], 4, 8), np.uint8)            # tile, grupo, fila
    for k, g in enumerate(grupo_de_par):
        out[:, g, :] = t[:, :, k]
    return out.tobytes()


def sprites_dotemu_a_neogeo(d: bytes) -> tuple[bytes, bytes]:
    """Bloque de sprites de DotEmu (tiles 16x16, 4bpp, fila mayor, 8 bytes por fila,
    nibble bajo = píxel izquierdo) a un par de ROMs C (impar: planos 0-1, par: planos 2-3).

    Formato Neo Geo por tile (128 bytes de la región entrelazada C impar/C par):
      mitad izquierda (x 0-7) en 0x40-0x7F, mitad derecha (x 8-15) en 0x00-0x3F,
      4 bytes por fila: +0 plano 0, +1 plano 2, +2 plano 1, +3 plano 3;
      dentro de cada byte, el píxel x de la mitad ocupa el bit (7 - x)
      (verificado contra el CRC de MAME: ver docs/ROM_MAP.md)."""
    t = np.frombuffer(d, np.uint8).reshape(-1, 16, 8)
    px = np.empty((t.shape[0], 16, 16), np.uint8)
    px[:, :, 0::2] = t & 15
    px[:, :, 1::2] = t >> 4
    pesos = (1 << np.arange(7, -1, -1)).astype(np.uint16)   # píxel 0 -> bit 7
    region = np.empty((t.shape[0], 128), np.uint8)
    for mitad, base in ((0, 0x40), (1, 0x00)):
        m = px[:, :, mitad * 8:(mitad + 1) * 8]
        for desplaz, plano in ((0, 0), (1, 2), (2, 1), (3, 3)):
            bits = ((m >> plano) & 1).astype(np.uint16) * pesos
            region[:, base + desplaz: base + 64: 4] = bits.sum(axis=2).astype(np.uint8)
    r = region.reshape(-1)
    return r[0::2].tobytes(), r[1::2].tobytes()


# ---------------------------------------------------------------- oráculo (MAME)

def esperados_de_mame(mame: Path) -> dict[str, dict]:
    xml = subprocess.run([str(mame), "-listxml", "mslugx"], capture_output=True, check=True,
                         cwd=mame.parent).stdout
    esperado = {}
    for m in ET.fromstring(xml).iter("machine"):
        for r in m.iter("rom"):
            if r.get("crc"):
                esperado.setdefault(r.get("name"), {
                    "crc": r.get("crc"), "sha1": r.get("sha1"),
                    "size": int(r.get("size")), "region": r.get("region"), "maquina": m.get("name")})
    return esperado


# ---------------------------------------------------------------- reconstrucción

def reconstruir(roms: Path) -> dict[str, tuple[str, bytes]]:
    """Devuelve {nombre_mame: (carpeta, datos)}."""
    leer = lambda n: (roms / n).read_bytes()
    out: dict[str, tuple[str, bytes]] = {}

    p = leer("mslugx_game_m68k")                      # mismo orden de bytes que los .p de MAME
    out["250-p1.p1"] = ("mslugx", p[:1 * MB])
    out["250-p2.ep1"] = ("mslugx", p[1 * MB:5 * MB])

    out["250-s1.s1"] = ("mslugx", fix_dotemu_a_neogeo(leer("mslugx_game_sfix")))
    out["250-m1.m1"] = ("mslugx", leer("mslugx_game_z80"))

    v = leer("mslugx_adpcm")
    out["250-v1.v1"] = ("mslugx", v[0:4 * MB])
    out["250-v2.v2"] = ("mslugx", v[4 * MB:8 * MB])
    out["250-v3.v3"] = ("mslugx", v[8 * MB:10 * MB])

    c = leer("mslugx_tiles")
    for k in range(3):
        impar, par = sprites_dotemu_a_neogeo(c[k * 16 * MB:(k + 1) * 16 * MB])
        out[f"250-c{2 * k + 1}.c{2 * k + 1}"] = ("mslugx", impar)
        out[f"250-c{2 * k + 2}.c{2 * k + 2}"] = ("mslugx", par)

    out["sp-u2.sp1"] = ("neogeo", leer("mslugx_bios_m68k"))
    out["sfix.sfix"] = ("neogeo", fix_dotemu_a_neogeo(leer("mslugx_bios_sfix")))
    out["000-lo.lo"] = ("neogeo", leer("mslugx_zoom_table"))
    return out


SUSTITUTOS = {
    # GOG no incluye el BIOS Z80 de la placa MVS. Como en la Neo Geo AES (que no tiene SM1),
    # el Z80 arranca desde el M1 del cartucho. MAME lo acepta con advertencia de checksum.
    # Decisión documentada en docs/ROM_MAP.md ("sm1.sm1: sustituto").
    "sm1.sm1": ("neogeo", "250-m1.m1"),
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    pd_roms = os.environ.get("PD_ROMS")
    pd_tools = os.environ.get("PD_TOOLS")
    ap.add_argument("--roms", type=Path, default=Path(pd_roms) if pd_roms else None)
    ap.add_argument("--salida", type=Path)
    ap.add_argument("--mame", type=Path, default=Path(pd_tools, "mame", "mame.exe") if pd_tools else None)
    a = ap.parse_args()
    if not a.roms or not a.mame:
        ap.error("activa el entorno (env.ps1) o indica --roms y --mame")
    salida = a.salida or (a.roms / "mame")

    esperado = esperados_de_mame(a.mame)
    datos = reconstruir(a.roms)
    sustituidos = set()
    for nombre, (carpeta, origen) in SUSTITUTOS.items():
        real = salida / carpeta / nombre
        if real.is_file() and f"{zlib.crc32(real.read_bytes()):08x}" == esperado[nombre]["crc"]:
            continue                                  # el usuario aportó el archivo auténtico
        datos[nombre] = (carpeta, datos[origen][1])
        sustituidos.add(nombre)

    print(f"{'archivo':<14} {'carpeta':<7} {'bytes':>9}  {'crc32':<8}  estado")
    fallas = 0
    for nombre, (carpeta, b) in datos.items():
        destino = salida / carpeta
        destino.mkdir(parents=True, exist_ok=True)
        (destino / nombre).write_bytes(b)
        crc = f"{zlib.crc32(b):08x}"
        sha1 = hashlib.sha1(b).hexdigest()
        e = esperado.get(nombre)
        if nombre in sustituidos:
            estado = f"SUSTITUTO ({SUSTITUTOS[nombre][1]}; MAME avisará checksum)"
        elif e is None:
            estado = "SIN REFERENCIA EN MAME"
        elif crc == e["crc"] and sha1 == e["sha1"] and len(b) == e["size"]:
            estado = "OK (CRC32+SHA1)"
        else:
            estado = f"DISTINTO (MAME espera crc {e['crc']})"
        fallas += not (estado.startswith("OK") or estado.startswith("SUSTITUTO")
                       or nombre == "250-p1.p1")       # P1 de GOG: diferencia conocida (ROM_MAP.md)
        print(f"{nombre:<14} {carpeta:<7} {len(b):>9}  {crc}  {estado}")

    faltan = sorted(n for n, e in esperado.items()
                    if n not in datos and (e["maquina"] == "mslugx" and "bios" not in e["region"]
                                           or e["region"] in ("audiobios", "fixedbios", "spritegen:zoomy")))
    if faltan:
        print("\nMAME también requiere (GOG no los incluye): " + ", ".join(faltan))
    print(f"\nSalida: {salida}")
    return 1 if fallas else 0


if __name__ == "__main__":
    sys.exit(main())
