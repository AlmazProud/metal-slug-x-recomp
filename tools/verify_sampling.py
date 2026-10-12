"""Verifica las reglas de muestreo de interrupciones (src/cpu/cycles68k.h, access_class) contra el
análisis del microcódigo del 68000 (tools/sampling_queues.py sobre la descripción de MAME,
usada solo como documentación).

Uso:
  python sampling_queues.py <mame>/src/devices/cpu/m68000/m68000-sdf.cpp > colas.txt
  python verify_sampling.py colas.txt build/src/clases_muestreo.exe
Compara, para cada manejador y cada camino, (L, U) desde el inicio de la instrucción. Se excluyen
MOVEM, CHK, DIV, RESET, TRAPV, STOP y MOVEP (caminos con bucles: ver docs/PHASE4_VERIFICATION.md).
"""
from __future__ import annotations

import ast
import re
import subprocess
import sys


def main() -> int:
    consultas, esperado = [], []
    for line in open(sys.argv[1], encoding="utf-8"):
        m = re.match(r"(\S+)\s+([0-9A-F]{4})/([0-9A-F]{4}) (.*)$", line.strip())
        if not m:
            continue
        name = m.group(1)
        if re.match(r"(movem|chk|div|reset|trapv|stop|movep)", name):
            continue
        op = int(m.group(2), 16)
        if re.match(r"b\w*_rel8", name):
            op |= 0x02                         # Bcc corto: el desplazamiento nunca es 0
        for dur, l, u in ast.literal_eval(m.group(4)):
            consultas.append(f"{op:04X} {dur}")
            esperado.append((name, dur, "N" if l is None else str(l), str(u)))
    output = subprocess.run([sys.argv[2]], input="\n".join(consultas), capture_output=True, text=True).stdout.split("\n")
    mal = 0
    for (n, d, l, u), r in zip(esperado, output):
        if r.split() != [l, u]:
            mal += 1
            if mal <= 20:
                print(f"DISTINTO {n} duración {d}: esperado {l} {u}, reglas {r}")
    print(f"casos: {len(esperado)}, distintos: {mal}")
    return 0 if mal == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
