"""Checks a MAME set (zips or folders) against MAME's own ROM list, by CRC-32 and SHA-1.

Usage:
    python tools/verify_set.py [--roms DIR] [--mame EXE] MACHINE [MACHINE...]

For every ROM of each machine (`mame -listxml`, BIOS files left out unless they belong to the
selected BIOS `--bios`, default `us`), the file is looked up by CRC-32 among every file under DIR
and every member of the zips there, then its SHA-1 is compared. Prints one line per ROM and a
summary; exit code 0 when every game ROM (the cartridge's, regions `cslot1:*`) is found with
matching hashes. System files (BIOS, sfix, sm1, zoom table) are listed but do not fail: the
workspace uses stand-ins for some of them.
"""
from __future__ import annotations

import argparse
import hashlib
import os
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def local_files(roms: Path) -> dict[int, list[tuple[str, bytes]]]:
    by_crc: dict[int, list[tuple[str, bytes]]] = {}
    for f in sorted(p for p in roms.rglob("*") if p.is_file()):
        if zipfile.is_zipfile(f):
            with zipfile.ZipFile(f) as z:
                for i in z.infolist():
                    if not i.is_dir():
                        by_crc.setdefault(i.CRC, []).append((f"{f.name}/{i.filename}", z.read(i)))
        elif not f.name.upper().startswith("MANIFEST"):
            d = f.read_bytes()
            by_crc.setdefault(zlib.crc32(d), []).append((str(f.relative_to(roms)), d))
    return by_crc


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("machines", nargs="+")
    ap.add_argument("--roms", type=Path, default=Path(os.environ.get("PD_ROMS") or ROOT / "roms"))
    pd_tools = os.environ.get("PD_TOOLS")
    ap.add_argument("--mame", type=Path, default=Path(pd_tools or ROOT.parent / "tools", "mame", "mame.exe"))
    ap.add_argument("--bios", default="us")
    a = ap.parse_args()

    have = local_files(a.roms)
    bad = 0
    for m in a.machines:
        xml = subprocess.run([str(a.mame), "-listxml", m], cwd=a.mame.parent, capture_output=True,
                             check=True).stdout
        mach = next(x for x in ET.fromstring(xml).iter("machine") if x.get("name") == m)
        for r in mach.iter("rom"):
            bios = r.get("bios")
            if bios and bios != a.bios:
                continue
            name, size = r.get("name"), int(r.get("size"))
            crc, sha1 = int(r.get("crc"), 16), r.get("sha1")
            hits = have.get(crc, [])
            ok = [n for n, d in hits if len(d) == size and hashlib.sha1(d).hexdigest() == sha1]
            if ok:
                state = "OK"
            elif hits:
                state = "SHA-1 MISMATCH"
            else:
                state = "NOT FOUND"
            system = not r.get("region", "").startswith("cslot")    # BIOS, sfix, sm1, zoom table
            if state != "OK" and not system:
                bad += 1
            print(f"{m:8} {name:14} {size:9} {crc:08x} {r.get('region'):24} {state:15} {ok[0] if ok else ''}")
    print("all game ROMs OK" if not bad else f"{bad} game ROM(s) missing or different")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
