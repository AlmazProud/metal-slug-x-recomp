"""Finds another game's profile windows in this game's P1 (Phase 4: Metal Slug 5's and 3's engine
routines in Metal Slug 4, as Metal Slug 5's port did with Metal Slug 3's).

For each window {start, end} of a reference profile (its WINDOWS table in src/games/<id>/profile.c
of the reference repository), the reference code is cut into overlapping chunks of --chunk bytes;
every exact occurrence of a chunk in the target's P1 votes for the displacement target - reference.
The best displacement, its votes and the share of the window's chunks found there are printed:
relocated code matches with every relative branch, and only its absolute operands (addresses of
other routines, of RAM) break chunks. Candidates must still be checked one by one (and in MAME).

Usage:
    python tools/match_windows.py --ref-profile PROFILE.c --ref-p REF_game_m68k --target TARGET_game_m68k
                                  [--chunk 8] [--top 3]

Prints addresses only (no ROM bytes).
"""
from __future__ import annotations

import argparse
import re
from collections import Counter, defaultdict
from pathlib import Path

MB = 1 << 20


def cpu_view(b: bytes) -> bytes:
    a = bytearray(b)
    a[0::2], a[1::2] = b[1::2], b[0::2]
    return bytes(a)


def windows(profile: Path):
    text = profile.read_text(encoding="utf-8")
    body = re.search(r"ProfileWindow WINDOWS\[\]\s*=\s*\{(.*?)\n\};", text, re.S).group(1)
    for m in re.finditer(r"\{\s*(0x[0-9A-Fa-f]+)\s*,\s*(0x[0-9A-Fa-f]+)\s*\}\s*,?\s*(/\*.*?\*/)?", body, re.S):
        note = re.sub(r"\s+", " ", (m.group(3) or "").strip("/* ").strip())
        yield int(m.group(1), 16), int(m.group(2), 16), note


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ref-profile", type=Path, required=True)
    ap.add_argument("--ref-p", type=Path, required=True)
    ap.add_argument("--target", type=Path, required=True)
    ap.add_argument("--chunk", type=int, default=8)
    ap.add_argument("--top", type=int, default=3)
    a = ap.parse_args()
    ref = cpu_view(a.ref_p.read_bytes()[:MB])
    tgt = cpu_view(a.target.read_bytes()[:MB])

    index: dict[bytes, list[int]] = defaultdict(list)
    n = a.chunk
    for o in range(0, len(tgt) - n, 2):
        index[tgt[o:o + n]].append(o)

    for start, end, note in windows(a.ref_profile):
        votes: Counter = Counter()
        chunks = list(range(start, max(start + 2, end - n + 1), 2))
        for o in chunks:
            hits = index.get(ref[o:o + n], [])
            if len(hits) <= 8:                      # frequent chunks (padding, rts) do not vote
                for h in hits:
                    votes[h - o] += 1
        best = votes.most_common(a.top)
        text = "  ".join(f"{start + d:06X} ({v}/{len(chunks)})" for d, v in best) or "not found"
        print(f"{start:06X}-{end:06X}  ->  {text}   {note[:70]}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
