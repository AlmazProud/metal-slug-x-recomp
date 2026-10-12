"""Genera las tests diferenciales del recompilador (una función C por instrucción).

Toma las instrucciones de P1 del listado del análisis (Fase 1), elige hasta N instancias reales
por cada opcode distinto (palabras de extensión distintas) y emite, para cada una, el C del
generador envuelto en una función. El arnés (differential.c) ejecuta cada instrucción en Musashi
y en el C generado desde estados aleatorios y compara el resultado.

La output contiene código derivado del juego: se escribe en el directorio de compilación y
nunca se versiona.

Uso: python generate_tests.py --roms DIR --listado ARCHIVO --output ARCHIVO.c [--por-opcode N]
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools" / "recomp68k"))
from decoder import decodificar          # noqa: E402
import generator                                  # noqa: E402
from generator import Emisor, NoSoportada       # noqa: E402

CABECERA = r"""/* GENERADO por tests/differential/generate_tests.py: NO EDITAR, NO VERSIONAR. */
#include <string.h>
#include "cpu68k.h"
#define CIC(n) (n)
#include "cycles68k.h"
#include "tests.h"

#define PD_SIGUIENTE(d)        do { c->pc = (d); return; } while (0)
#define PD_SALTO(d)            do { c->pc = (d); return; } while (0)
#define PD_SALTO_DIN(e)        do { c->pc = (e); return; } while (0)
#define PD_LLAMADA(d, r)       PD_SALTO(d)
#define PD_LLAMADA_DIN(e, r)   PD_SALTO_DIN(e)
#define PD_RETORNO(e)          PD_SALTO_DIN(e)
#define PD_EXCEPCION(v, pc)    do { cpu_exception(c, (v), (pc)); return; } while (0)
#define PD_SR_CAMBIADO(d)      PD_SIGUIENTE(d)
#define PD_STOP(d)             PD_SIGUIENTE(d)
#define PD_RESET_EXTERNO()     ((void)0)
#define PD_INI(o)              do { c->op = (o); c->start_cycle = c->cycles; } while (0)

"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", type=Path, required=True)
    ap.add_argument("--listado", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--por-opcode", type=int, default=2)
    ap.add_argument("--cycles", type=Path, help="tabla de cycles base (volcar_ciclos)")
    a = ap.parse_args()
    if a.cycles:
        generator.cargar_ciclos(a.cycles)

    raw = (a.roms / "mslugx_game_m68k").read_bytes()[:1 << 20]
    cpu = bytearray(len(raw))
    cpu[0::2], cpu[1::2] = raw[1::2], raw[0::2]
    leer16 = lambda d: int.from_bytes(cpu[d:d + 2], "big") if d + 2 <= len(cpu) else 0  # noqa: E731

    elegidas: dict[int, list] = defaultdict(list)
    vistas_ext: set[bytes] = set()
    for line in a.listado.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^([0-9A-F]{6})", line)
        if not m:
            continue
        d = int(m.group(1), 16)
        i = decodificar(leer16, d)
        if i is None:
            continue
        cuerpo = bytes(cpu[d:d + i.largo])
        if len(elegidas[i.opcode]) >= a.por_opcode or cuerpo in vistas_ext:
            continue
        vistas_ext.add(cuerpo)
        elegidas[i.opcode].append(i)

    funciones, tabla, no_soportadas = [], [], Counter()
    for opcode in sorted(elegidas):
        for i in elegidas[opcode]:
            try:
                cuerpo = Emisor(i).emitir()
            except NoSoportada as e:
                no_soportadas[str(e)] += 1
                continue
            name = f"i_{i.dir:06X}"
            funciones.append(f"static void {name}(Cpu68k *c)\n{{\n    " + "\n    ".join(cuerpo) + "\n}\n")
            tabla.append(f'    {{0x{i.dir:06X}u, {name}, "{i.text()}"}},')

    a.output.parent.mkdir(parents=True, exist_ok=True)
    a.output.write_text(CABECERA + "\n".join(funciones) +
                        "\nconst Test tests[] = {\n" + "\n".join(tabla) + "\n};\n" +
                        f"const int n_tests = {len(tabla)};\n", encoding="utf-8")
    print(f"tests generadas: {len(tabla)} (opcodes distintos: {len(elegidas)})")
    for k, n in no_soportadas.most_common():
        print(f"  NO SOPORTADA: {k} x{n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
