"""Compara trazas de ciclos por instrucción del 68000: MAME (tools/mame/cycles.lua) contra
el port (variable PD_TRAZA_INSTR). Cada línea útil empieza por "C=<ciclo> <PC>:".

Uso:
  python compare_cycles.py MAME.txt PORT.txt [--max 20]

Alinea ambas trazas en la primera instrucción de MAME (mismo PC, ciclo más cercano) y las recorre
a la par. Informa el desfase inicial, cada instrucción cuya duración difiere (con su
desensamblado de MAME) y la primera divergencia de flujo (PC distinto). Las entradas a
interrupciones se reconocen por la línea "(interrupted at ...)" de MAME.
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

LINEA = re.compile(r"^C=(\d+) ([0-9A-F]+):\s*(.*)$")


def leer(ruta: Path) -> list[tuple[int, int, str]]:
    r = []
    for l in ruta.read_text(encoding="utf-8", errors="replace").splitlines():
        m = LINEA.match(l)
        if m:
            r.append((int(m.group(1)), int(m.group(2), 16), m.group(3).strip()))
    return r


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("mame", type=Path)
    ap.add_argument("port", type=Path)
    ap.add_argument("--max", type=int, default=20)
    a = ap.parse_args()
    m, p = leer(a.mame), leer(a.port)
    c0, pc0, _ = m[0]
    cand = [i for i, (c, pc, _) in enumerate(p) if pc == pc0]
    if not cand:
        print("el PC inicial de MAME no aparece en la traza del port")
        return 1
    j = min(cand, key=lambda i: abs(p[i][0] - c0))
    print(f"alineado: MAME C={c0} PC={pc0:06X}; port C={p[j][0]} (desfase port-MAME {p[j][0] - c0:+d} ciclos)")
    distintas = Counter()
    ejemplos = {}
    n = 0
    for i in range(len(m) - 1):
        if j + 1 >= len(p):
            break
        (cm, pcm, txt), (cm2, pcm2, _) = m[i], m[i + 1]
        (cp, pcp, _), (cp2, pcp2, _) = p[j], p[j + 1]
        if pcm != pcp:
            print(f"FLUJO DISTINTO en la instrucción {i}: MAME {pcm:06X} ({txt}) C={cm}; port {pcp:06X} C={cp} "
                  f"(desfase {cp - cm:+d})")
            break
        dm, dp = cm2 - cm, cp2 - cp
        if pcm2 == pcp2 and dm != dp:
            clave = (txt.split()[0] if txt else "?", pcm)
            distintas[clave] += 1
            if clave not in ejemplos:
                ejemplos[clave] = f"{pcm:06X} {txt:<32} MAME {dm} ciclos, port {dp} (siguiente {pcm2:06X})"
        n += 1
        j += 1
    print(f"instrucciones recorridas a la par: {n}")
    print(f"instrucciones con duración distinta: {sum(distintas.values())} (en {len(distintas)} sitios)")
    for clave, k in distintas.most_common(a.max):
        print(f"  x{k:<6} {ejemplos[clave]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
