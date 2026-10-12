"""Genera las pruebas diferenciales del recompilador (una función C por instrucción).

Toma las instrucciones de P1 del listado del análisis (Fase 1), elige hasta N instancias reales
por cada opcode distinto (palabras de extensión distintas) y emite, para cada una, el C del
generador envuelto en una función. El arnés (diferencial.c) ejecuta cada instrucción en Musashi
y en el C generado desde estados aleatorios y compara el resultado.

La salida contiene código derivado del juego: se escribe en el directorio de compilación y
nunca se versiona.

Uso: python generar_pruebas.py --roms DIR --listado ARCHIVO --salida ARCHIVO.c [--por-opcode N]
"""
from __future__ import annotations

import argparse
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "herramientas" / "recomp68k"))
from decodificador import decodificar          # noqa: E402
from generador import Emisor, NoSoportada       # noqa: E402

CABECERA = r"""/* GENERADO por tests/diferencial/generar_pruebas.py: NO EDITAR, NO VERSIONAR. */
#include <string.h>
#include "cpu68k.h"
#include "pruebas.h"

#define PD_SIGUIENTE(d)        do { c->pc = (d); return; } while (0)
#define PD_SALTO(d)            do { c->pc = (d); return; } while (0)
#define PD_SALTO_DIN(e)        do { c->pc = (e); return; } while (0)
#define PD_LLAMADA(d, r)       PD_SALTO(d)
#define PD_LLAMADA_DIN(e, r)   PD_SALTO_DIN(e)
#define PD_RETORNO(e)          PD_SALTO_DIN(e)
#define PD_EXCEPCION(v, pc)    do { cpu_excepcion(c, (v), (pc)); return; } while (0)
#define PD_SR_CAMBIADO(d)      PD_SIGUIENTE(d)
#define PD_STOP(d)             PD_SIGUIENTE(d)
#define PD_RESET_EXTERNO()     ((void)0)

"""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", type=Path, required=True)
    ap.add_argument("--listado", type=Path, required=True)
    ap.add_argument("--salida", type=Path, required=True)
    ap.add_argument("--por-opcode", type=int, default=2)
    a = ap.parse_args()

    raw = (a.roms / "mslugx_game_m68k").read_bytes()[:1 << 20]
    cpu = bytearray(len(raw))
    cpu[0::2], cpu[1::2] = raw[1::2], raw[0::2]
    leer16 = lambda d: int.from_bytes(cpu[d:d + 2], "big") if d + 2 <= len(cpu) else 0  # noqa: E731

    elegidas: dict[int, list] = defaultdict(list)
    vistas_ext: set[bytes] = set()
    for linea in a.listado.read_text(encoding="utf-8").splitlines():
        m = re.match(r"^([0-9A-F]{6})", linea)
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
            nombre = f"i_{i.dir:06X}"
            funciones.append(f"static void {nombre}(Cpu68k *c)\n{{\n    " + "\n    ".join(cuerpo) + "\n}\n")
            tabla.append(f'    {{0x{i.dir:06X}u, {nombre}, "{i.texto()}"}},')

    a.salida.parent.mkdir(parents=True, exist_ok=True)
    a.salida.write_text(CABECERA + "\n".join(funciones) +
                        "\nconst Prueba pruebas[] = {\n" + "\n".join(tabla) + "\n};\n" +
                        f"const int n_pruebas = {len(tabla)};\n", encoding="utf-8")
    print(f"pruebas generadas: {len(tabla)} (opcodes distintos: {len(elegidas)})")
    for k, n in no_soportadas.most_common():
        print(f"  NO SOPORTADA: {k} x{n}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
