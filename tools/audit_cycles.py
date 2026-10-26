"""Audits the recompiler's cycle table against the real 68000 cycles from MAME.

Usage:
  python audit_cycles.py --roms FOLDER --cycles cycles_68000.bin MAME_TRACE.txt [...]

Reads traces from tools/mame/cycles.lua ("C=<cycle> <PC>: <instruction>"), takes the duration
of each uninterrupted instruction (difference with the next line) and compares it with the
base table dumped from Musashi (the one the generator uses). Only code in P1 and in the
BIOS is considered (the opcode is read from the user's ROM). Instructions with a dynamic
duration are reported separately with all their observed durations, to be checked by hand
against the generator's rules.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

LINE = re.compile(r"^C=(\d+) ([0-9A-F]+):\s*(.*)$")
DYNAMIC = ("b", "db", "s", "as", "ls", "ro", "rox", "movem", "mul", "div", "jmp", "jsr", "rts", "rte", "trap", "chk")


def is_dynamic(txt: str) -> bool:
    mn = txt.split()[0] if txt else ""
    base = mn.split(".")[0]
    return (base.startswith(("asl", "asr", "lsl", "lsr", "rol", "ror", "roxl", "roxr", "movem", "mul", "div"))
            or (base.startswith("b") and base not in ("bset", "bclr", "bchg", "btst"))
            or base.startswith("db") or (base.startswith("s") and len(base) <= 3 and base not in ("sub", "swap"))
            or base in ("jmp", "jsr", "rts", "rte", "rtr", "trap", "trapv", "chk", "stop", "reset", "move" if "SR" in txt else ""))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", type=Path, required=True)
    ap.add_argument("--game", default="mslug3", help="DotEmu file prefix (mslug3, mslugx)")
    ap.add_argument("--cycles", type=Path, required=True)
    ap.add_argument("traces", type=Path, nargs="+")
    a = ap.parse_args()

    def view(name: str) -> bytes:
        d = bytearray((a.roms / name).read_bytes())
        d[0::2], d[1::2] = d[1::2], d[0::2]
        return bytes(d)

    p, bios = view(f"{a.game}_game_m68k"), view(f"{a.game}_bios_m68k")
    table = a.cycles.read_bytes()

    def opcode(pc: int) -> int | None:
        if pc < 0x100000:
            return p[pc] << 8 | p[pc + 1]
        if 0xC00000 <= pc < 0xC20000:
            o = pc - 0xC00000
            return bios[o] << 8 | bios[o + 1]
        return None

    obs: dict[int, Counter] = defaultdict(Counter)
    text: dict[int, str] = {}
    for t in a.traces:
        rows, breaks = [], set()
        for l in t.read_text(encoding="utf-8", errors="replace").splitlines():
            if "interrupted" in l:
                breaks.add(len(rows))
                continue
            m = LINE.match(l)
            if m:
                rows.append((int(m.group(1)), int(m.group(2), 16), m.group(3).strip()))
        for i in range(len(rows) - 1):
            if i + 1 in breaks:
                continue
            c, pc, txt = rows[i]
            op = opcode(pc)
            if op is None:
                continue
            obs[op][rows[i + 1][0] - c] += 1
            text.setdefault(op, txt)

    fixed, dyn = [], []
    for op, cnt in sorted(obs.items()):
        txt = text[op]
        if is_dynamic(txt):
            dyn.append((op, txt, cnt))
        elif set(cnt) != {table[op]}:
            fixed.append((op, txt, cnt))
    print(f"opcodes observed: {len(obs)}; fixed ones that do not match the table: {len(fixed)}")
    for op, txt, cnt in fixed:
        print(f"  {op:04X} {txt:<34} table {table[op]:3d}  MAME {dict(cnt)}")
    print(f"dynamic (check against the generator's rules): {len(dyn)}")
    for op, txt, cnt in dyn:
        print(f"  {op:04X} {txt:<34} table {table[op]:3d}  MAME {dict(cnt)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
