"""Compares the port's per-frame fingerprints with MAME's (Phase 4).

Formats (tools/mame/fingerprints.lua and the port's --fingerprints option):
  V <n> <RAM hash> <palette hash>   on every VBlank acknowledge (n = order since power-on)
  F <n> <image hash>                image of frame n

Notes:
  - V fingerprints are taken on entry to the VBlank handler, where both sides are on an
    instruction boundary.
  - screen:pixels() in MAME's frame notifier returns the previous frame's image:
    the port's F <n> is compared with MAME's F <n + 1>.

Usage:
  python compare_fingerprints.py MAME.txt PORT.txt
Returns 0 if everything matches; 1 if there are differences (and lists them).
"""
from __future__ import annotations

import sys
from pathlib import Path


def read_fingerprints(path: Path) -> tuple[dict[int, str], dict[int, str]]:
    v, f = {}, {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        p = line.split()
        if len(p) >= 4 and p[0] == "V":
            v[int(p[1])] = p[2] + " " + p[3]
        elif len(p) >= 3 and p[0] == "F":
            f[int(p[1])] = p[2]
    return v, f


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    mv, mf = read_fingerprints(Path(sys.argv[1]))
    pv, pf = read_fingerprints(Path(sys.argv[2]))
    common_v = sorted(set(mv) & set(pv))
    diff_v = [n for n in common_v if mv[n] != pv[n]]
    common_f = sorted(n for n in pf if n + 1 in mf)
    diff_f = [n for n in common_f if pf[n] != mf[n + 1]]
    print(f"VBlank (RAM + palette): {len(common_v)} compared, {len(diff_v)} differ"
          f" (MAME {len(mv)}, port {len(pv)})")
    print(f"Image per frame:        {len(common_f)} compared, {len(diff_f)} differ")
    if diff_v:
        print("  first differing VBlanks:", diff_v[:10])
    if diff_f:
        print("  first differing frames:", diff_f[:10])
    ok = not diff_v and not diff_f and len(common_v) == len(mv) and len(common_f) >= len(mf) - 2
    print("NO DIVERGENCES" if ok else "DIVERGENCES FOUND")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
