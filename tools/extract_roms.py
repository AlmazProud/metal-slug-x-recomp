"""Rebuilds the MAME set (mslugx + neogeo) from the DotEmu/GOG files.

Usage:
    python tools/extract_roms.py [--roms DIR] [--output DIR] [--mame EXE]

- Reads the mslugx_* files from --roms (%PD_ROMS% by default).
- Writes <output>/mslugx/*.{p1,ep1,s1,m1,v*,c*} and <output>/neogeo/* (<roms>/mame by default).
- Gets the expected names and hashes from `mame -listxml` (MAME is the oracle),
  checks the CRC32 and SHA1 of each rebuilt file and prints a report.

The formats and transformations are documented in docs/ROM_MAP.md.
This script contains no ROM data: only transformations.
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


# ---------------------------------------------------------------- transformations

def fix_dotemu_to_neogeo(d: bytes) -> bytes:
    """DotEmu 8x8 fix (4bpp, row-major, 4 bytes per row, low nibble = left pixel)
    to the Neo Geo S format (per 32-byte tile: 4 column groups of 8 bytes,
    one byte per row; groups in pixel order 4-5, 6-7, 0-1, 2-3)."""
    t = np.frombuffer(d, np.uint8).reshape(-1, 8, 4)        # tile, row, pixel pair
    group_of_pair = (2, 3, 0, 1)                            # pair k -> 8-byte group in the S tile
    out = np.empty((t.shape[0], 4, 8), np.uint8)            # tile, group, row
    for k, g in enumerate(group_of_pair):
        out[:, g, :] = t[:, :, k]
    return out.tobytes()


def sprites_dotemu_to_neogeo(d: bytes) -> tuple[bytes, bytes]:
    """DotEmu sprite block to a pair of C ROMs (odd: planes 0-1, even: planes 2-3)."""
    r = sprites_dotemu_to_region(d)
    return r[0::2].tobytes(), r[1::2].tobytes()


def sprites_dotemu_to_region(d: bytes) -> np.ndarray:
    """DotEmu sprite block (16x16 tiles, 4bpp, row-major, 8 bytes per row,
    low nibble = left pixel) to MAME's sprite region (even bytes: odd C ROM, planes 0-1; odd
    bytes: even C ROM, planes 2-3). Also used by tools/check_load.py.

    Neo Geo format per tile (128 bytes of the interleaved odd C/even C region):
      left half (x 0-7) at 0x40-0x7F, right half (x 8-15) at 0x00-0x3F,
      4 bytes per row: +0 plane 0, +1 plane 2, +2 plane 1, +3 plane 3;
      within each byte, pixel x of the half occupies bit (7 - x)
      (verified against MAME's CRC: see docs/ROM_MAP.md)."""
    t = np.frombuffer(d, np.uint8).reshape(-1, 16, 8)
    px = np.empty((t.shape[0], 16, 16), np.uint8)
    px[:, :, 0::2] = t & 15
    px[:, :, 1::2] = t >> 4
    weights = (1 << np.arange(7, -1, -1)).astype(np.uint16)   # pixel 0 -> bit 7
    region = np.empty((t.shape[0], 128), np.uint8)
    for half, base in ((0, 0x40), (1, 0x00)):
        m = px[:, :, half * 8:(half + 1) * 8]
        for offset, plane in ((0, 0), (1, 2), (2, 1), (3, 3)):
            bits = ((m >> plane) & 1).astype(np.uint16) * weights
            region[:, base + offset: base + 64: 4] = bits.sum(axis=2).astype(np.uint8)
    return region.reshape(-1)


# ---------------------------------------------------------------- oracle (MAME)

def expected_from_mame(mame: Path) -> dict[str, dict]:
    xml = subprocess.run([str(mame), "-listxml", "mslugx"], capture_output=True, check=True,
                         cwd=mame.parent).stdout
    expected = {}
    for m in ET.fromstring(xml).iter("machine"):
        for r in m.iter("rom"):
            if r.get("crc"):
                expected.setdefault(r.get("name"), {
                    "crc": r.get("crc"), "sha1": r.get("sha1"),
                    "size": int(r.get("size")), "region": r.get("region"), "machine": m.get("name")})
    return expected


# ---------------------------------------------------------------- rebuild

def rebuild(roms: Path) -> dict[str, tuple[str, bytes]]:
    """Returns {mame_name: (folder, data)}."""
    read = lambda n: (roms / n).read_bytes()
    out: dict[str, tuple[str, bytes]] = {}

    p = read("mslugx_game_m68k")                      # same byte order as MAME's .p files
    out["250-p1.p1"] = ("mslugx", p[:1 * MB])
    out["250-p2.ep1"] = ("mslugx", p[1 * MB:5 * MB])

    out["250-s1.s1"] = ("mslugx", fix_dotemu_to_neogeo(read("mslugx_game_sfix")))
    out["250-m1.m1"] = ("mslugx", read("mslugx_game_z80"))

    v = read("mslugx_adpcm")
    out["250-v1.v1"] = ("mslugx", v[0:4 * MB])
    out["250-v2.v2"] = ("mslugx", v[4 * MB:8 * MB])
    out["250-v3.v3"] = ("mslugx", v[8 * MB:10 * MB])

    c = read("mslugx_tiles")
    for k in range(3):
        odd, even = sprites_dotemu_to_neogeo(c[k * 16 * MB:(k + 1) * 16 * MB])
        out[f"250-c{2 * k + 1}.c{2 * k + 1}"] = ("mslugx", odd)
        out[f"250-c{2 * k + 2}.c{2 * k + 2}"] = ("mslugx", even)

    out["sp-u2.sp1"] = ("neogeo", read("mslugx_bios_m68k"))
    out["sfix.sfix"] = ("neogeo", fix_dotemu_to_neogeo(read("mslugx_bios_sfix")))
    out["000-lo.lo"] = ("neogeo", read("mslugx_zoom_table"))
    return out


SUBSTITUTES = {
    # GOG does not include the Z80 BIOS of the MVS board. As on the Neo Geo AES (which has no SM1),
    # the Z80 boots from the cartridge's M1. MAME accepts it with a checksum warning.
    # Decision documented in docs/ROM_MAP.md (the sm1.sm1 substitute).
    "sm1.sm1": ("neogeo", "250-m1.m1"),
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    pd_roms = os.environ.get("PD_ROMS")
    pd_tools = os.environ.get("PD_TOOLS")
    ap.add_argument("--roms", type=Path, default=Path(pd_roms) if pd_roms else None)
    ap.add_argument("--output", type=Path)
    ap.add_argument("--mame", type=Path, default=Path(pd_tools, "mame", "mame.exe") if pd_tools else None)
    a = ap.parse_args()
    if not a.roms or not a.mame:
        ap.error("activate the environment (env.ps1) or pass --roms and --mame")
    output = a.output or (a.roms / "mame")

    expected = expected_from_mame(a.mame)
    data = rebuild(a.roms)
    substituted = set()
    for name, (folder, source) in SUBSTITUTES.items():
        real = output / folder / name
        if real.is_file() and f"{zlib.crc32(real.read_bytes()):08x}" == expected[name]["crc"]:
            continue                                  # the user supplied the genuine file
        data[name] = (folder, data[source][1])
        substituted.add(name)

    print(f"{'file':<14} {'folder':<7} {'bytes':>9}  {'crc32':<8}  status")
    failures = 0
    for name, (folder, b) in data.items():
        target = output / folder
        target.mkdir(parents=True, exist_ok=True)
        (target / name).write_bytes(b)
        crc = f"{zlib.crc32(b):08x}"
        sha1 = hashlib.sha1(b).hexdigest()
        e = expected.get(name)
        if name in substituted:
            status = f"SUBSTITUTE ({SUBSTITUTES[name][1]}; MAME will warn about the checksum)"
        elif e is None:
            status = "NO REFERENCE IN MAME"
        elif crc == e["crc"] and sha1 == e["sha1"] and len(b) == e["size"]:
            status = "OK (CRC32+SHA1)"
        else:
            status = f"DIFFERENT (MAME expects crc {e['crc']})"
        failures += not (status.startswith("OK") or status.startswith("SUBSTITUTE")
                         or name == "250-p1.p1")       # GOG's P1: known difference (ROM_MAP.md)
        print(f"{name:<14} {folder:<7} {len(b):>9}  {crc}  {status}")

    missing = sorted(n for n, e in expected.items()
                     if n not in data and (e["machine"] == "mslugx" and "bios" not in e["region"]
                                            or e["region"] in ("audiobios", "fixedbios", "spritegen:zoomy")))
    if missing:
        print("\nMAME also requires (GOG does not include them): " + ", ".join(missing))
    print(f"\nOutput: {output}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
