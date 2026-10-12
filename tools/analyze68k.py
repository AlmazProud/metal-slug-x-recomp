"""Static analysis of the mslugx 68000 code (Phase 1).

Recursive disassembly (Capstone) over the CPU view of P1 and of the BIOS, rooted at:
  - the 68000 vectors of P1 (0x000-0x0FF),
  - the entry points that the Neo Geo header exposes to the BIOS (JMP at 0x122/0x128/0x12E/0x134),
  - the executed addresses recorded by dynamic coverage (tools/coverage.py),
  - the observed targets of indirect jumps.

Produces:
  - <root>/analysis/listing_p1.txt      annotated disassembly (outside the repo: contains the game)
  - docs/functions_p1.csv               function list (addresses and metrics only)
  - docs/PHASE1_COVERAGE.md             coverage report

Usage:
    python tools/analyze68k.py [--coverage analysis/coverage.json]
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

import capstone
from capstone import m68k as M

MB = 1 << 20
TERMINATORS = {"rts", "rte", "rtr", "jmp", "bra", "illegal", "stop"}
# Root reasons below are tokens written to the listing and to docs/functions_p1.csv: keep them.
HEURISTICS = {"puntero_en_codigo", "tabla_punteros"}
COND_BRANCHES = {"bra", "bhi", "bls", "bcc", "bcs", "bne", "beq", "bvc", "bvs",
                 "bpl", "bmi", "bge", "blt", "bgt", "ble", "bhs", "blo"}


def base_mn(i) -> str:
    """Mnemonic without size suffix: 'bra.s' -> 'bra', 'btst.l' -> 'btst'."""
    return i.mnemonic.split(".")[0]
VECTORS = {0: "SSP", 1: "RESET", 2: "BUS_ERROR", 3: "ADDRESS_ERROR", 4: "ILLEGAL", 5: "ZERO_DIVIDE",
           6: "CHK", 7: "TRAPV", 8: "PRIVILEGE", 9: "TRACE", 10: "LINE_A", 11: "LINE_F",
           24: "SPURIOUS", 25: "IRQ1_VBLANK", 26: "IRQ2_TIMER", 27: "IRQ3", 28: "IRQ4", 29: "IRQ5",
           30: "IRQ6", 31: "IRQ7"}
HEADER = {0x122: "USER", 0x128: "PLAYER_START", 0x12E: "DEMO_END", 0x134: "COIN_SOUND"}


def cpu_view(b: bytes) -> bytes:
    """.p / .sp1 files (MAME order) -> big-endian memory as the 68000 sees it."""
    a = bytearray(b)
    a[0::2], a[1::2] = b[1::2], b[0::2]
    return bytes(a)


class Memory:
    def __init__(self, roms: Path) -> None:
        p = cpu_view((roms / "mslugx_game_m68k").read_bytes())
        self.regions = [(0x000000, p[:MB], "p1"),
                        (0xC00000, cpu_view((roms / "mslugx_bios_m68k").read_bytes()), "bios")]

    def region(self, addr: int):
        for base, data, name in self.regions:
            if base <= addr < base + len(data):
                return base, data, name
        return None

    def u32(self, addr: int) -> int:
        base, d, _ = self.region(addr)
        o = addr - base
        return int.from_bytes(d[o:o + 4], "big")


class Analyzer:
    def __init__(self, mem: Memory) -> None:
        self.mem = mem
        self.cs = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
        self.cs.detail = True
        self.ins: dict[int, object] = {}            # address -> instruction
        self.successors: dict[int, list[int]] = {}  # intraprocedural flow
        self.calls: dict[int, list[int]] = defaultdict(list)
        self.inputs: dict[int, set[str]] = defaultdict(set)
        self.unresolved_indirect: set[int] = set()
        self.data_refs: Counter = Counter()
        self.invalid: set[int] = set()
        self.origin: dict[int, str] = {}           # address -> reason of the root that reached it
        self.forbidden_data: set[int] = set()      # words that MAME read as data
        self.data_cuts = 0

    def decode(self, addr: int):
        r = self.mem.region(addr)
        if r is None or addr & 1:
            return None
        base, d, _ = r
        o = addr - base
        for i in self.cs.disasm(d[o:o + 10], addr, count=1):
            return None if i.mnemonic.startswith("dc.") else i   # dc.w = invalid opcode
        return None

    def target(self, i) -> int | None:
        """Static target of a jump/call, or None if it is indirect."""
        for op in i.operands:
            if op.type == M.M68K_OP_BR_DISP:
                return i.address + 2 + op.br_disp.disp
            if op.type == M.M68K_OP_MEM:
                am = op.address_mode
                if am in (M.M68K_AM_ABSOLUTE_DATA_LONG, M.M68K_AM_ABSOLUTE_DATA_SHORT):
                    return op.imm & 0xFFFFFF if op.imm else self._abs_from_text(i)
                if am == M.M68K_AM_PCI_DISP:
                    return i.address + 2 + op.mem.disp
                return None
        return self._abs_from_text(i)

    @staticmethod
    def _abs_from_text(i) -> int | None:
        m = re.search(r"\$([0-9a-f]+)", i.op_str)
        return int(m.group(1), 16) & 0xFFFFFF if m and "(" not in i.op_str else None

    def explore(self, roots: dict[int, str], observed_indirect: dict[int, set[int]]) -> None:
        pending = []
        for a, reason in roots.items():
            if reason != "dinamica":          # executed code is an exploration root, not a function start
                self.inputs[a].add(reason)
            pending.append((a, reason))
        while pending:
            a, reason = pending.pop()
            while a not in self.ins:
                i = self.decode(a)
                if i is None:
                    self.invalid.add(a)
                    break
                if reason in HEURISTICS and any(o in self.forbidden_data
                                                for o in range(a & ~1, a + i.size, 2)):
                    self.data_cuts += 1           # the heuristic would overwrite proven data
                    break
                self.ins[a] = i
                self.origin[a] = reason
                mn = base_mn(i)
                nxt = a + i.size
                succ = []
                self._refs(i)
                if mn in ("bsr", "jsr"):
                    t = self.target(i)
                    targets = [t] if t is not None else sorted(observed_indirect.get(a, ()))
                    if t is None and not targets:
                        self.unresolved_indirect.add(a)
                    for t in targets:
                        self.calls[a].append(t)
                        self.inputs[t].add("llamada")
                        pending.append((t, reason))
                    succ.append(nxt)
                elif mn == "jmp" or mn in COND_BRANCHES or mn.startswith("db"):
                    t = self.target(i)
                    targets = [t] if t is not None else sorted(observed_indirect.get(a, ()))
                    if t is None and not targets:
                        self.unresolved_indirect.add(a)
                    for t in targets:
                        succ.append(t)
                        pending.append((t, reason))
                    if mn not in TERMINATORS:
                        succ.append(nxt)
                elif mn not in TERMINATORS:
                    succ.append(nxt)
                self.successors[a] = succ
                if mn in TERMINATORS or not succ or succ[-1] != nxt:
                    break
                a = nxt

    def _refs(self, i) -> None:
        """Absolute data references into P1 (lea/move/etc. with absolute addresses)."""
        mn = base_mn(i)
        if mn in ("jmp", "jsr", "bsr") or mn in COND_BRANCHES or mn.startswith("db"):
            return
        for m in re.finditer(r"\$([0-9a-f]{4,6})\.l", i.op_str):
            v = int(m.group(1), 16)
            if v < MB:
                self.data_refs[v] += 1
        for op in i.operands:
            if op.type == M.M68K_OP_MEM and op.address_mode == M.M68K_AM_PCI_DISP:
                self.data_refs[i.address + 2 + op.mem.disp] += 1

    # ---------------------------------------------------------------- heuristics
    def looks_like_code(self, addr: int, max_ins: int = 64) -> bool:
        """Valid candidate: even address in P1 that decodes without invalid opcodes up to a
        terminator, without landing in the middle of an already known instruction and without
        starting in padding."""
        if addr & 1 or not (0x200 <= addr < MB):
            return False
        if addr in self.ins:
            return True
        p1 = self.mem.regions[0][1]
        if p1[addr:addr + 4] in (b"\x00\x00\x00\x00", b"\xff\xff\xff\xff"):
            return False
        a = addr
        for _ in range(max_ins):
            if a in self.ins:
                return True                      # joins known code at an instruction boundary
            if self.inside_instruction(a):
                return False
            i = self.decode(a)
            if i is None or any(o in self.forbidden_data for o in range(a & ~1, a + i.size, 2)):
                return False
            mn = base_mn(i)
            if mn in ("rts", "rte", "rtr", "jmp", "bra"):
                return True
            a += i.size
        return False

    def inside_instruction(self, a: int) -> bool:
        for d in (2, 4, 6, 8):
            i = self.ins.get(a - d)
            if i is not None and i.size > d:
                return True
        return False

    def pointers_in_code(self) -> set[int]:
        """Constants in confirmed code that point into P1 (move.l #imm / lea / pea abs)."""
        cand = set()
        for i in list(self.ins.values()):
            if i.address >= MB:
                continue
            for m in re.finditer(r"#\$([0-9a-f]{3,6})\b|\$([0-9a-f]{3,6})\.l", i.op_str):
                v = int(m.group(1) or m.group(2), 16)
                if v < MB and v not in self.ins and self.looks_like_code(v):
                    cand.add(v)
        return cand

    def pointer_tables(self, minimum: int = 3) -> set[int]:
        """Runs of >= `minimum` consecutive longwords, outside code, that point to valid code."""
        p1 = self.mem.regions[0][1]
        covered = bytearray(MB)
        for a, i in self.ins.items():
            if a < MB:
                covered[a:a + i.size] = b"\x01" * i.size
        cand, run = set(), []
        o = 0
        while o + 4 <= MB:
            v = int.from_bytes(p1[o:o + 4], "big")
            if not covered[o] and v < MB and self.looks_like_code(v):
                run.append(v)
                o += 4
                continue
            if len(run) >= minimum:
                cand.update(x for x in run if x not in self.ins)
            run = []
            o += 2
        if len(run) >= minimum:
            cand.update(x for x in run if x not in self.ins)
        return cand

    def explore_with_heuristics(self, roots, observed_indirect, use_heuristics: bool) -> None:
        self.explore(roots, observed_indirect)
        if not use_heuristics:
            return
        while True:                                   # A: pointers in code, until a fixed point
            new = self.pointers_in_code()
            if not new:
                break
            self.explore({v: "puntero_en_codigo" for v in new}, observed_indirect)
        while True:                                   # B: pointer tables (and whatever they uncover)
            new = self.pointer_tables()
            if not new:
                break
            self.explore({v: "tabla_punteros" for v in new}, observed_indirect)
            while True:
                n2 = self.pointers_in_code()
                if not n2:
                    break
                self.explore({v: "puntero_en_codigo" for v in n2}, observed_indirect)

    # ---------------------------------------------------------------- functions
    def functions(self, start: int = 0, end: int = MB) -> list[dict]:
        out = []
        for e in sorted(x for x in self.inputs if x in self.ins and start <= x < end):
            seen, stack = set(), [e]
            while stack:
                a = stack.pop()
                if a in seen or a not in self.ins:
                    continue
                seen.add(a)
                stack.extend(self.successors.get(a, ()))
            size = sum(self.ins[a].size for a in seen)
            # Keys match the columns of docs/functions_p1.csv.
            out.append({"inicio": e, "fin": max(a + self.ins[a].size for a in seen),
                        "bytes_codigo": size, "instrucciones": len(seen),
                        "llamadas": sum(len(self.calls.get(a, ())) for a in seen),
                        "motivos": sorted(self.inputs[e])})
        return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    root = Path(os.environ.get("PD_ROOT", Path(__file__).resolve().parents[2]))
    repo = Path(__file__).resolve().parents[1]
    ap.add_argument("--roms", type=Path, default=root / "roms")
    ap.add_argument("--coverage", type=Path, nargs="+", default=[root / "analysis" / "coverage.json"],
                    help="one or more coverage JSON files (tools/coverage.py); they are merged")
    a = ap.parse_args()

    mem = Memory(a.roms)
    p1 = mem.regions[0][1]
    cov = {"pcs": {}, "indirectos": [], "lecturas_p1": [], "instrucciones_trazadas": 0}
    for path in a.coverage:
        if not path.exists():
            continue
        part = json.loads(path.read_text())
        for region, pcs in part["pcs"].items():
            cov["pcs"][region] = sorted(set(cov["pcs"].get(region, [])) | set(pcs))
        cov["indirectos"] = sorted({tuple(x) for x in cov["indirectos"]} | {tuple(x) for x in part["indirectos"]})
        cov["lecturas_p1"] = sorted(set(cov["lecturas_p1"]) | set(part.get("lecturas_p1", [])))
        cov["instrucciones_trazadas"] += part.get("instrucciones_trazadas", 0)
    dyn_p1 = set(cov["pcs"].get("p1", []))
    dyn_bios = set(cov["pcs"].get("bios", []))
    observed_indirect: dict[int, set[int]] = defaultdict(set)
    for src, dst, _bank in cov["indirectos"]:
        observed_indirect[src].add(dst)

    roots: dict[int, str] = {}
    for n, name in VECTORS.items():
        if n == 0:
            continue
        v = mem.u32(n * 4)
        if v < MB:
            roots[v] = f"vector_{name}"
    for off, name in HEADER.items():
        roots.setdefault(off, f"cabecera_{name}")
    static_roots = dict(roots)
    for pc in dyn_p1 | dyn_bios:
        roots.setdefault(pc, "dinamica")

    # 1) Validation: static analysis alone (no MAME) against what MAME executed.
    alone = Analyzer(mem)
    alone.explore_with_heuristics(static_roots, {}, use_heuristics=True)
    recall = (sum(1 for pc in dyn_p1 if pc in alone.ins) / len(dyn_p1)) if dyn_p1 else 0.0
    alone_no_heur = Analyzer(mem)
    alone_no_heur.explore(static_roots, {})
    recall_no_heur = (sum(1 for pc in dyn_p1 if pc in alone_no_heur.ins) / len(dyn_p1)) if dyn_p1 else 0.0

    # MAME ground truth: bytes read as data. These are the reads from P1 minus the bytes of
    # executed instructions, with a 4-byte margin after each one (68000 prefetch).
    data_read: set[int] = set()
    if cov.get("lecturas_p1"):
        executed = set()
        for pc in dyn_p1:
            i = alone.decode(pc)
            if i is not None:
                executed.update(range(pc, pc + i.size + 4))
        data_read = {w for w in cov["lecturas_p1"] if w not in executed and w + 1 not in executed}

    # 2) Combined analysis: roots + dynamic coverage + heuristics that do not overwrite that data.
    an = Analyzer(mem)
    an.forbidden_data = data_read
    an.explore_with_heuristics(roots, observed_indirect, use_heuristics=True)

    # ------------------------------------------------ P1 coverage
    code = bytearray(MB)
    by_evidence: Counter = Counter()
    for addr, i in an.ins.items():
        if addr < MB:
            code[addr:addr + i.size] = b"\x01" * i.size
            o = an.origin.get(addr, "?")
            kind = o if o in ("puntero_en_codigo", "tabla_punteros") else "confirmado"
            by_evidence[kind] += i.size
    static_bytes = sum(code)
    dyn_bytes = sum(an.ins[p].size for p in dyn_p1 if p in an.ins)
    refs_outside = sum(1 for r in an.data_refs if r < MB and not code[r])
    # padding: runs of 16+ bytes of 0x00 or 0xFF outside code
    padding = 0
    for m in re.finditer(rb"(\x00{16,}|\xff{16,})", p1):
        padding += sum(1 for o in range(m.start(), m.end()) if not code[o])
    data_or_unknown = MB - static_bytes - padding

    def false_positives(instructions) -> int:
        """Instruction bytes that MAME proved to be data (in 2-byte words)."""
        return 2 * sum(1 for a_, n in instructions for o in range(a_ & ~1, a_ + n, 2) if o in data_read)

    fp = Counter()
    for addr, i in an.ins.items():
        if addr < MB:
            o = an.origin.get(addr, "?")
            kind = o if o in ("puntero_en_codigo", "tabla_punteros") else "confirmado"
            fp[kind] += false_positives([(addr, i.size)])

    # ------------------------------------------------ cross-check against Ghidra
    ghidra_txt = "(Ghidra was not run; see tools/ghidra/)"
    gcsv = root / "analysis" / "ghidra" / "instrucciones_ghidra.csv"
    if gcsv.exists():
        g_ins = {}
        for line in gcsv.read_text().split():
            d, n = line.split(",")
            g_ins[int(d, 16)] = int(n)
        g_bytes = sum(g_ins.values())
        g_recall = sum(1 for pc in dyn_p1 if pc in g_ins) / len(dyn_p1) if dyn_p1 else 0
        ours = {a_ for a_ in an.ins if a_ < MB}
        both = ours & set(g_ins)
        g_covers = bytearray(MB)
        for d, n in g_ins.items():
            g_covers[d:d + n] = b"\x01" * n
        conflict = sum(1 for a_ in ours if a_ not in g_ins and g_covers[a_])
        g_fp = false_positives(g_ins.items())
        ghidra_txt = f"""Ghidra 12.1.4 headless, P1 as a raw big-endian 68000 binary, with only the static
roots (vectors and header) and its auto-analysis (`tools/ghidra/`).

| Measure | This analysis (combined) | Ghidra |
|---|---:|---:|
| Instruction bytes | {static_bytes:,} | {g_bytes:,} |
| Addresses executed in MAME that it recognizes | {100 * sum(1 for pc in dyn_p1 if pc in an.ins) / max(1, len(dyn_p1)):.1f} % | {100 * g_recall:.1f} % |
| Bytes marked as code that MAME read as data (certain false positives) | {sum(fp.values()):,} | {g_fp:,} |

- Instruction starts in common: {len(both):,}.
- Our instructions that land in the middle of a Ghidra instruction (misalignment; one
  of the two is wrong): {conflict:,}.
- Without MAME data, this analysis recognizes {100 * recall:.1f} % of what was executed; Ghidra,
  also without MAME, {100 * g_recall:.1f} %."""

    funcs = an.functions()
    bios_calls = Counter(t for src, ts in an.calls.items() if src < MB for t in ts if t >= 0xC00000)
    bios_jumps = Counter(an.target(i) for a_, i in an.ins.items()
                         if a_ < MB and base_mn(i) == "jmp" and (an.target(i) or 0) >= 0xC00000)
    unresolved_p1 = sorted(x for x in an.unresolved_indirect if x < MB)

    # ------------------------------------------------ outputs
    listing = root / "analysis" / "listing_p1.txt"
    listing.parent.mkdir(parents=True, exist_ok=True)
    starts = {f["inicio"] for f in funcs}
    with listing.open("w", encoding="utf-8") as f:
        for addr in sorted(x for x in an.ins if x < MB):
            if addr in starts:
                f.write(f"\n; ---- funcion {addr:06X}  ({', '.join(sorted(an.inputs[addr]))})\n")
            i = an.ins[addr]
            mark = "*" if addr in dyn_p1 else " "
            f.write(f"{addr:06X}{mark} {i.mnemonic:<8} {i.op_str}\n")

    # BIOS listing (code reached from what was executed in MAME), same format.
    bios_funcs = {fn["inicio"] for fn in an.functions(0xC00000, 0xC20000)}
    with (root / "analysis" / "listing_bios.txt").open("w", encoding="utf-8") as f:
        for addr in sorted(x for x in an.ins if 0xC00000 <= x < 0xC20000):
            if addr in bios_funcs:
                f.write(f"\n; ---- funcion {addr:06X}  ({', '.join(sorted(an.inputs[addr]))})\n")
            i = an.ins[addr]
            mark = "*" if addr in dyn_bios else " "
            f.write(f"{addr:06X}{mark} {i.mnemonic:<8} {i.op_str}\n")

    with (repo / "docs" / "functions_p1.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["start", "end", "code_bytes", "instructions", "calls", "reasons"])
        for fn in funcs:
            w.writerow([f"{fn['inicio']:06X}", f"{fn['fin']:06X}", fn["bytes_codigo"], fn["instrucciones"],
                        fn["llamadas"], " ".join(fn["motivos"])])

    vec_txt = "\n".join(f"| {n} | `{n * 4:#05x}` | {name} | `{mem.u32(n * 4):06X}` |"
                        for n, name in VECTORS.items())
    hdr_txt = "\n".join(f"| `{off:#05x}` | {name} | `{an.ins[off].mnemonic} {an.ins[off].op_str}` |"
                        if off in an.ins else f"| `{off:#05x}` | {name} | (does not decode) |"
                        for off, name in HEADER.items())
    bios_txt = "\n".join(f"| `{t:06X}` | {n} | {bios_jumps.get(t, 0)} |"
                         for t, n in sorted((bios_calls + Counter()).items()))
    for t, n in bios_jumps.items():
        if t not in bios_calls:
            bios_txt += f"\n| `{t:06X}` | 0 | {n} |"
    regions = ", ".join(f"{k}: {len(v)}" for k, v in sorted(cov["pcs"].items()))
    not_covered = sorted(p for p in dyn_p1 if p not in an.ins)

    report = f"""# Phase 1: 68000 analysis coverage report

Generated by `tools/analyze68k.py` (Capstone {capstone.__version__}) from the user's ROMs
and the dynamic coverage from MAME (`tools/coverage.py`,
{cov.get('instrucciones_trazadas', 0):,} traced instructions).

## P1 (`0x000000`–`0x0FFFFF`, 1 MB)

| Measure | Bytes | % |
|---|---:|---:|
| Code (total) | {static_bytes:,} | {100 * static_bytes / MB:.1f} |
| ...confirmed: reached from vectors, header, direct calls or execution in MAME | {by_evidence['confirmado']:,} | {100 * by_evidence['confirmado'] / MB:.1f} |
| ...heuristic A: pointer loaded as a constant by confirmed code | {by_evidence['puntero_en_codigo']:,} | {100 * by_evidence['puntero_en_codigo'] / MB:.1f} |
| ...heuristic B: table of 3 or more pointers to valid code | {by_evidence['tabla_punteros']:,} | {100 * by_evidence['tabla_punteros'] / MB:.1f} |
| ...of the total, executed in MAME (dynamic coverage) | {dyn_bytes:,} | {100 * dyn_bytes / MB:.1f} |
| Padding (runs of 16 or more bytes of 0x00 or 0xFF outside code) | {padding:,} | {100 * padding / MB:.1f} |
| Data or unknown | {data_or_unknown:,} | {100 * data_or_unknown / MB:.1f} |

- Functions detected: **{len(funcs)}** (full list in `docs/functions_p1.csv`).
- **Validation of the heuristics against MAME:** of the {len(dyn_p1):,} P1 addresses that
  MAME executed, the purely static analysis (no MAME data) finds
  **{100 * recall:.1f} %** with heuristics and {100 * recall_no_heur:.1f} % without them.
- Decoded instructions: {sum(1 for x in an.ins if x < MB):,}.
- Data addresses referenced outside code: {refs_outside:,}.
- **Unresolved** indirect jumps or calls in P1 (no target observed in MAME):
  {len(unresolved_p1)}. First ones: {', '.join(f'`{x:06X}`' for x in unresolved_p1[:15])}
- Addresses executed in MAME that the static analysis does not reach: {len(not_covered)}
  (must be 0; otherwise there is a decoding bug).
- Invalid start addresses (do not decode): {len([x for x in an.invalid if x < MB])}.

## Precision: code that MAME proved to be data

MAME recorded {len(data_read):,} P1 words read as data rather than as instructions.
Bytes marked as code that fall on those words, by evidence:

| Evidence | Code bytes | Certain false positives |
|---|---:|---:|
| Confirmed | {by_evidence['confirmado']:,} | {fp['confirmado']:,} |
| Heuristic A (pointer in code) | {by_evidence['puntero_en_codigo']:,} | {fp['puntero_en_codigo']:,} |
| Heuristic B (pointer table) | {by_evidence['tabla_punteros']:,} | {fp['tabla_punteros']:,} |

This is a lower bound: it only detects the data the game read during the trace. In the combined
analysis, heuristic paths that would overwrite that data are cut ({an.data_cuts} cuts),
so they should be 0 here. The purely static validation does not use this information.

## Cross-check against Ghidra

{ghidra_txt}

## Dynamic coverage by region (distinct instruction addresses)

{regions}

## 68000 vectors (in P1)

| # | Offset | Vector | Target |
|---|---|---|---|
{vec_txt}

## Neo Geo header entry points

| Offset | Entry point | Instruction |
|---|---|---|
{hdr_txt}

## Calls and jumps into the BIOS (`0xC00000`–`0xC0FFFF`)

| Target | Calls (jsr/bsr) | Jumps (jmp) |
|---|---:|---:|
{bios_txt}
"""
    (repo / "docs" / "PHASE1_COVERAGE.md").write_text(report, encoding="utf-8")
    print(report)
    print(f"Listing: {listing}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
