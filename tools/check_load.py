"""Checks what the runtime loaded (PD_DUMP_ROMS) against the regions MAME builds.

Usage:
    python tools/check_load.py --exe mslug4.exe --roms DIR [--decrypted DIR] [--work DIR]

Runs `exe --roms DIR --headless` with PD_DUMP_ROMS, then converts the runtime's parts back to
MAME's layouts with the Metal Slug 3 port's verified conversions (tools/extract_roms.py) and
compares them with MAME's regions dumped by tools/mame/dump_region.lua (--decrypted, default
<roms>/decrypted: maincpu.bin, sprites.bin, fixed.bin, audiocpu.bin, adpcma.bin) and the BIOS files
(<roms>/neogeo). Exit code 0 when every part is identical. No ROM data here: only comparisons.
"""
from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from extract_roms import fix_dotemu_to_neogeo, sprites_dotemu_to_region  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--roms", type=Path, required=True)
    ap.add_argument("--decrypted", type=Path, default=None)
    ap.add_argument("--work", type=Path, default=None)
    a = ap.parse_args()
    dec = a.decrypted or a.roms / "decrypted"
    work = a.work or Path(tempfile.mkdtemp(prefix="check_load_"))
    work.mkdir(parents=True, exist_ok=True)
    r = subprocess.run([str(a.exe), "--roms", str(a.roms), "--headless", "--frames", "1", "--no-audio"],
                       env=dict(os.environ, PD_DUMP_ROMS=str(work), PD_ROMS_INFO="1"),
                       capture_output=True, text=True)
    print(r.stdout.splitlines()[0] if r.stdout else r.stderr)
    if r.returncode != 0:
        print(r.stderr)
        return 1
    part = lambda n: (work / f"{n}.bin").read_bytes()
    ref = lambda n: (dec / n).read_bytes()
    p = bytearray(part("p"))
    p[0::2], p[1::2] = p[1::2], p[0::2]                       # back to MAME's region byte order
    audiocpu = ref("audiocpu.bin")
    checks = [
        ("p", bytes(p), ref("maincpu.bin")),
        ("tiles", sprites_dotemu_to_region(part("tiles")).tobytes(), ref("sprites.bin")),
        ("fix_game", fix_dotemu_to_neogeo(part("fix_game")), ref("fixed.bin")),
        ("z80", part("z80"), audiocpu[0x10000:0x10000 + len(part("z80"))]),
        ("adpcm", part("adpcm"), ref("adpcma.bin")),
        ("fix_bios", fix_dotemu_to_neogeo(part("fix_bios")), (a.roms / "neogeo" / "sfix.sfix").read_bytes()),
        ("zoom", part("zoom"), (a.roms / "neogeo" / "000-lo.lo").read_bytes()),
    ]
    bios = bytearray(part("bios"))
    bios[0::2], bios[1::2] = bios[1::2], bios[0::2]
    checks.append(("bios", bytes(bios), (a.roms / "neogeo" / "sp-u2.sp1").read_bytes()))
    bad = 0
    for name, got, want in checks:
        same = got == want
        if not same:
            n = min(len(got), len(want))
            d = int(np.count_nonzero(np.frombuffer(got[:n], np.uint8) != np.frombuffer(want[:n], np.uint8)))
            print(f"{name:9} DIFFERENT ({len(got)} vs {len(want)} bytes, {d} differ)")
            bad += 1
        else:
            print(f"{name:9} identical ({len(got)} bytes)")
    print("all identical" if not bad else f"{bad} part(s) differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
