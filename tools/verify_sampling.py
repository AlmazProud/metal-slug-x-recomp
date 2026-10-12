"""Checks the interrupt sampling rules (src/cpu/cycles68k.h, access_class) against the
analysis of the 68000 microcode (tools/sampling_queues.py over MAME's description,
used only as documentation).

Usage:
  python sampling_queues.py <mame>/src/devices/cpu/m68000/m68000-sdf.cpp > colas.txt
  python verify_sampling.py colas.txt build/src/sampling_classes.exe
Compares, for each handler and each path, (L, U) from the start of the instruction. Excluded:
MOVEM, CHK, DIV, RESET, TRAPV, STOP and MOVEP (paths with loops: see docs/PHASE4_VERIFICATION.md).
"""
from __future__ import annotations

import ast
import re
import subprocess
import sys


def main() -> int:
    queries, expected = [], []
    for line in open(sys.argv[1], encoding="utf-8"):
        m = re.match(r"(\S+)\s+([0-9A-F]{4})/([0-9A-F]{4}) (.*)$", line.strip())
        if not m:
            continue
        name = m.group(1)
        if re.match(r"(movem|chk|div|reset|trapv|stop|movep)", name):
            continue
        op = int(m.group(2), 16)
        if re.match(r"b\w*_rel8", name):
            op |= 0x02                         # short Bcc: the displacement is never 0
        for dur, l, u in ast.literal_eval(m.group(4)):
            queries.append(f"{op:04X} {dur}")
            expected.append((name, dur, "N" if l is None else str(l), str(u)))
    output = subprocess.run([sys.argv[2]], input="\n".join(queries), capture_output=True, text=True).stdout.split("\n")
    bad = 0
    for (n, d, l, u), r in zip(expected, output):
        if r.split() != [l, u]:
            bad += 1
            if bad <= 20:
                print(f"DIFFERENT {n} duration {d}: expected {l} {u}, rules {r}")
    print(f"cases: {len(expected)}, different: {bad}")
    return 0 if bad == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
