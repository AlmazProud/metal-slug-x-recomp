"""Compares our own decoder with Capstone over every instruction in the P1 listing.

Compares length in bytes and base mnemonic. Differences are grouped by kind for manual review:
either side may be wrong (Capstone decodes some 68020 formats even in 68000 mode).

Usage: python tools/recomp68k/validate_decoder.py
"""
from __future__ import annotations

import os
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from decoder import decode  # noqa: E402

ROOT = Path(os.environ.get("PD_ROOT", Path(__file__).resolve().parents[3]))


def capstone_name(i) -> str:
    """Our canonical name, expressed the way Capstone writes it."""
    if i.op == "bcc":
        return "b" + i.cc
    if i.op == "dbcc":
        return "dbra" if i.cc == "f" else "db" + i.cc
    if i.op == "scc":
        return "s" + i.cc
    if i.op.startswith("movem"):
        return "movem"
    if i.op.endswith("_ccr") or i.op.endswith("_sr"):
        return i.op.split("_")[0]
    if i.op in ("move_to_ccr", "move_to_sr", "move_from_sr", "move_to_usp", "move_from_usp"):
        return "move"
    return i.op


def main() -> int:
    game = sys.argv[1] if len(sys.argv) > 1 else "mslug3"          # DotEmu prefix
    raw = (ROOT / "roms" / f"{game}_game_m68k").read_bytes()[:1 << 20]
    cpu = bytearray(len(raw))
    cpu[0::2], cpu[1::2] = raw[1::2], raw[0::2]
    read16 = lambda a: int.from_bytes(cpu[a:a + 2], "big") if a + 2 <= len(cpu) else 0  # noqa: E731

    import capstone
    cs = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
    total = 0
    mismatches = Counter()
    examples = {}
    for line in (ROOT / "analysis" / "listing_p1.txt").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^([0-9A-F]{6})[* ] (\S+)", line)
        if not m:
            continue
        addr = int(m.group(1), 16)
        cap = m.group(2).split(".")[0]
        total += 1
        i = decode(read16, addr)
        cap_length = next(cs.disasm(bytes(cpu[addr:addr + 10]), addr, count=1)).size
        if i is None:
            key = f"ours: invalid / capstone: {cap}"
        elif capstone_name(i) != cap:
            key = f"name: ours {capstone_name(i)} / capstone {cap}"
        elif i.length != cap_length:
            key = f"length: ours {i.length} / capstone {cap_length} ({cap})"
        else:
            continue
        mismatches[key] += 1
        examples.setdefault(key, f"{addr:06X} {line[8:]}")
    # lengths: compared with the next address in the listing when it is contiguous
    print(f"instructions compared: {total}")
    print(f"matching mnemonic: {total - sum(mismatches.values())}")
    for k, n in mismatches.most_common(40):
        print(f"  {n:6d}  {k:<55} e.g. {examples[k]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
