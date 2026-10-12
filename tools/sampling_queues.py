"""68000 interrupt sampling tails (and effective tails), derived from the microcode description that
MAME uses as documentation (src/devices/cpu/m68000/m68000-sdf.cpp; no code is copied).

For each instruction handler it walks every path and measures how many cycles remain from
the last step at which the 68000 decides whether to take an interrupt
("m_next_state = m_int_next_state") to the end of the instruction. It ignores the
suspension blocks ("if(m_icount <= 0) {...}") and the address-error blocks, which are not the normal path.

Effective tail: cycles from the last bus-access end before (or at) the sampling point to
the end; None if there is no access before the sampling point within the instruction (the last access
is the end of the previous instruction). MAME only processes an IRQ raised at T when the CPU
finishes an access with T <= end; sampling points before that do not see it.

Output per path: (duration, L, U), measured from the start of the instruction: L = last
access end before or at the sampling point (None: none, the previous instruction's applies) and U = last
access end of the instruction.

Usage: python sampling_queues.py PATH/m68000-sdf.cpp
Prints one pattern (value/mask) per handler with its set of tails, used to write the
per-family rules of src/cpu/cycles68k.h.
"""
from __future__ import annotations

import re
import sys
from collections import defaultdict


def strip_blocks(lines: list[str]) -> list[str]:
    """Removes the conditional suspension and address-error blocks."""
    out, i = [], 0
    while i < len(lines):
        l = lines[i].strip()
        if l.startswith("if(m_icount <= 0) {") or l.startswith("if(m_aob & 1) {"):
            depth = l.count("{") - l.count("}")
            i += 1
            while i < len(lines) and depth > 0:
                depth += lines[i].count("{") - lines[i].count("}")
                i += 1
            continue
        out.append(l)
        i += 1
    return out


def walk_paths(lines: list[str], start: int, labels: dict[str, int], sample: int | None,
               cycles: int, visits: int, res: set, total: int = 0, access: int | None = None,
               effective: int | None = None):
    if visits > 12:                  # microcode loops (MOVEM, shifts): a few paths are enough
        return
    i = start
    while i < len(lines):
        l = lines[i]
        if "m_next_state = m_int_next_state" in l:
            sample, cycles = 0, 0
            effective = total - access if access is not None else None
        m = re.search(r"m_icount -= (\d+);", l)
        if m:
            cycles += int(m.group(1))
            total += int(m.group(1))
            if any("_interruptible(" in x or ".read" in x or ".write" in x for x in lines[max(0, i - 3):i]):
                access = total
        if l.startswith("m_inst_state = m_next_state ?") or (l == "return;" and i == len(lines) - 1):
            if sample is not None:
                res.add((total, None if effective is None else total - (effective + cycles), access))
            return
        m = re.match(r"goto (\w+);", l)
        if m:
            if visits > 20:
                return
            walk_paths(lines, labels[m.group(1)] + 1, labels, sample, cycles, visits + 1, res, total, access, effective)
            return
        if l.startswith("if(m_t)") or l.startswith("if(!m_t)") or re.match(r"if\(.*\)$", l):
            # branch "if(cond) goto A; else goto B;"
            nxt = lines[i + 1] if i + 1 < len(lines) else ""
            mm = re.match(r"goto (\w+);", nxt)
            if mm and i + 3 < len(lines) and lines[i + 2] == "else":
                mm2 = re.match(r"goto (\w+);", lines[i + 3])
                walk_paths(lines, labels[mm.group(1)] + 1, labels, sample, cycles, visits + 1, res, total, access, effective)
                if mm2:
                    walk_paths(lines, labels[mm2.group(1)] + 1, labels, sample, cycles, visits + 1, res, total, access, effective)
                return
        i += 1
    if sample is not None:
        res.add((total, None if effective is None else total - (effective + cycles), access))


def main() -> int:
    src = open(sys.argv[1], encoding="utf-8").read()
    for f in re.split(r"\nvoid m68000_device::", src)[1:]:
        m = re.match(r"(\w+)_df\(\)\s*//\s*([0-9a-f]{4})\s+([0-9a-f]{4})", f)
        if not m:
            continue
        body = f.split("\n", 1)[1]
        body = body[:body.rfind("}")]
        lines = [l for l in strip_blocks(body.splitlines()) if l and not l.startswith("//")]
        labels = {l[:-1]: k for k, l in enumerate(lines) if re.fullmatch(r"\w+:", l)}
        res: set = set()
        walk_paths(lines, 0, labels, None, 0, 0, res)
        print(f"{m.group(1):<28} {m.group(2).upper()}/{m.group(3).upper()} {sorted(res, key=str)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
