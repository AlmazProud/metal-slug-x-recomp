"""Compares the port's screenshots (port_NNNNNN.bmp) with MAME's (mame_NNNNNN.png) frame by frame.

Usage:
  python compare_screenshots.py --port FOLDER --mame FOLDER [--output FOLDER]

For each frame present in both folders, reports how many pixels differ and, with --output,
writes a MAME | port | differences (in red) image for review.
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import images  # noqa: E402


def frames(folder: Path, prefix: str, ext: str) -> dict[int, Path]:
    r = {}
    for f in folder.glob(f"{prefix}_*.{ext}"):
        m = re.fullmatch(rf"{prefix}_(\d+)\.{ext}", f.name)
        if m:
            r[int(m.group(1))] = f
    return r


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=Path, required=True)
    ap.add_argument("--mame", type=Path, required=True)
    ap.add_argument("--output", type=Path)
    a = ap.parse_args()

    p = frames(a.port, "port", "bmp")
    m = frames(a.mame, "mame", "png")
    common = sorted(set(p) & set(m))
    if not common:
        print("No frames in common")
        return 1
    if a.output:
        a.output.mkdir(parents=True, exist_ok=True)
    total_ok = 0
    for n in common:
        wp, hp, rp = images.read(p[n])
        wm, hm, rm = images.read(m[n])
        if (wp, hp) != (wm, hm):
            print(f"frame {n}: different sizes port {wp}x{hp}, MAME {wm}x{hm}")
            continue
        differing = sum(1 for i in range(0, len(rp), 3) if rp[i:i + 3] != rm[i:i + 3])
        status = "IDENTICAL" if differing == 0 else f"{differing} differing pixels ({100 * differing / (wp * hp):.2f}%)"
        print(f"frame {n:6d}: {status}")
        total_ok += differing == 0
        if a.output and differing:
            diff = bytearray()
            for i in range(0, len(rp), 3):
                diff += b"\xff\x00\x00" if rp[i:i + 3] != rm[i:i + 3] else bytes(v // 4 for v in rm[i:i + 3])
            rows = bytearray()
            for y in range(hp):
                s = slice(y * wp * 3, (y + 1) * wp * 3)
                rows += rm[s] + rp[s] + diff[s]
            images.write_png(a.output / f"cmp_{n:06d}.png", images.scale((wp * 3, hp, bytes(rows)), 2))
    print(f"identical: {total_ok}/{len(common)}")
    return 0 if total_ok == len(common) else 1


if __name__ == "__main__":
    sys.exit(main())
