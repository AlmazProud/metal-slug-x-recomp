"""Compares per-instruction 68000 cycle traces: MAME (tools/mame/cycles.lua) against
the port (PD_TRACE_INSTR variable). Each useful line starts with "C=<cycle> <PC>:".

Usage:
  python compare_cycles.py MAME.txt PORT.txt [--max 20]

Aligns both traces on MAME's first instruction (same PC, closest cycle) and walks them
in lockstep. Reports the initial offset, every instruction whose duration differs (with its
MAME disassembly) and the first control-flow divergence (different PC). Interrupt entries
are recognized by MAME's "(interrupted at ...)" line.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

LINE = re.compile(r"^C=(\d+) ([0-9A-F]+):\s*(.*)$")
REGS = re.compile(r" D[0-7]=[0-9A-F]{8}")


def read_trace(path: Path) -> list[tuple[int, int, str]]:
    r = []
    for l in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINE.match(REGS.sub("", l))                 # registers (PD_TRACE_REGS) are ignored
        if m:
            r.append((int(m.group(1)), int(m.group(2), 16), m.group(3).strip()))
    return r


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("mame", type=Path)
    ap.add_argument("port", type=Path)
    ap.add_argument("--max", type=int, default=20)
    a = ap.parse_args()
    m, p = read_trace(a.mame), read_trace(a.port)
    c0, pc0, _ = m[0]
    cand = [i for i, (c, pc, _) in enumerate(p) if pc == pc0]
    if not cand:
        print("MAME's initial PC does not appear in the port trace")
        return 1
    j = min(cand, key=lambda i: abs(p[i][0] - c0))
    print(f"aligned: MAME C={c0} PC={pc0:06X}; port C={p[j][0]} (port-MAME offset {p[j][0] - c0:+d} cycles)")
    differing = Counter()
    examples = {}
    n = 0
    for i in range(len(m) - 1):
        if j + 1 >= len(p):
            break
        (cm, pcm, txt), (cm2, pcm2, _) = m[i], m[i + 1]
        (cp, pcp, _), (cp2, pcp2, _) = p[j], p[j + 1]
        if pcm != pcp:
            print(f"FLOW DIVERGES at instruction {i}: MAME {pcm:06X} ({txt}) C={cm}; port {pcp:06X} C={cp} "
                  f"(offset {cp - cm:+d})")
            break
        dm, dp = cm2 - cm, cp2 - cp
        if pcm2 == pcp2 and dm != dp:
            key = (txt.split()[0] if txt else "?", pcm)
            differing[key] += 1
            if key not in examples:
                examples[key] = f"{pcm:06X} {txt:<32} MAME {dm} cycles, port {dp} (next {pcm2:06X})"
        n += 1
        j += 1
    print(f"instructions walked in lockstep: {n}")
    print(f"instructions with a different duration: {sum(differing.values())} (at {len(differing)} sites)")
    for key, k in differing.most_common(a.max):
        print(f"  x{k:<6} {examples[key]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
