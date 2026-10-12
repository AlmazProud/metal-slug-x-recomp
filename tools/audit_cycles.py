"""Audita la tabla de cycles del recompilador contra los cycles reales del 68000 de MAME.

Uso:
  python audit_cycles.py --roms CARPETA --cycles ciclos_68000.bin TRAZA_MAME.txt [...]

Lee trazas de tools/mame/cycles.lua ("C=<ciclo> <PC>: <instrucción>"), toma la duración
de cada instrucción no interrumpida (diferencia con la línea siguiente) y la compara con la
tabla base volcada de Musashi (la que usa el generador). Solo considera código en P1 y en el
BIOS (el opcode se lee de la ROM del usuario). Las instrucciones de duración dinámica se
informan aparte con todas sus duraciones observadas, para revisarlas a mano contra las reglas
del generador.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

LINEA = re.compile(r"^C=(\d+) ([0-9A-F]+):\s*(.*)$")
DINAMICAS = ("b", "db", "s", "as", "ls", "ro", "rox", "movem", "mul", "div", "jmp", "jsr", "rts", "rte", "trap", "chk")


def es_dinamica(txt: str) -> bool:
    mn = txt.split()[0] if txt else ""
    base = mn.split(".")[0]
    return (base.startswith(("asl", "asr", "lsl", "lsr", "rol", "ror", "roxl", "roxr", "movem", "mul", "div"))
            or (base.startswith("b") and base not in ("bset", "bclr", "bchg", "btst"))
            or base.startswith("db") or (base.startswith("s") and len(base) <= 3 and base not in ("sub", "swap"))
            or base in ("jmp", "jsr", "rts", "rte", "rtr", "trap", "trapv", "chk", "stop", "reset", "move" if "SR" in txt else ""))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", type=Path, required=True)
    ap.add_argument("--cycles", type=Path, required=True)
    ap.add_argument("trazas", type=Path, nargs="+")
    a = ap.parse_args()

    def vista(name: str) -> bytes:
        d = bytearray((a.roms / name).read_bytes())
        d[0::2], d[1::2] = d[1::2], d[0::2]
        return bytes(d)

    p, bios = vista("mslugx_game_m68k"), vista("mslugx_bios_m68k")
    tabla = a.cycles.read_bytes()

    def opcode(pc: int) -> int | None:
        if pc < 0x100000:
            return p[pc] << 8 | p[pc + 1]
        if 0xC00000 <= pc < 0xC20000:
            o = pc - 0xC00000
            return bios[o] << 8 | bios[o + 1]
        return None

    obs: dict[int, Counter] = defaultdict(Counter)
    text: dict[int, str] = {}
    for t in a.trazas:
        filas, cortes = [], set()
        for l in t.read_text(encoding="utf-8", errors="replace").splitlines():
            if "interrupted" in l:
                cortes.add(len(filas))
                continue
            m = LINEA.match(l)
            if m:
                filas.append((int(m.group(1)), int(m.group(2), 16), m.group(3).strip()))
        for i in range(len(filas) - 1):
            if i + 1 in cortes:
                continue
            c, pc, txt = filas[i]
            op = opcode(pc)
            if op is None:
                continue
            obs[op][filas[i + 1][0] - c] += 1
            text.setdefault(op, txt)

    fijas, dinam = [], []
    for op, cnt in sorted(obs.items()):
        txt = text[op]
        if es_dinamica(txt):
            dinam.append((op, txt, cnt))
        elif set(cnt) != {tabla[op]}:
            fijas.append((op, txt, cnt))
    print(f"opcodes observados: {len(obs)}; fijos que no coinciden con la tabla: {len(fijas)}")
    for op, txt, cnt in fijas:
        print(f"  {op:04X} {txt:<34} tabla {tabla[op]:3d}  MAME {dict(cnt)}")
    print(f"dinámicos (revisar contra las reglas del generador): {len(dinam)}")
    for op, txt, cnt in dinam:
        print(f"  {op:04X} {txt:<34} tabla {tabla[op]:3d}  MAME {dict(cnt)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
