"""Genera el programa recompilado completo (P1 y BIOS del usuario) como funciones C.

Entradas de función:
  - inicios de función del análisis (listados de la Fase 1),
  - destinos observados de saltos indirectos (cobertura dinámica de MAME),
  - direcciones de retorno de cada JSR/BSR (continuaciones: permiten reanudar en código nativo
    cuando una llamada no vuelve "limpia").

Cada función se emite con una etiqueta por instrucción. Los saltos a instrucciones de la misma
función son `goto`; los saltos hacia atrás pasan antes por un punto de control (límite de
cycles -> despachador) para que los bucles de espera no bloqueen las interrupciones. Lo demás
vuelve al despachador con c->pc.

La output se deriva del programa del juego: se escribe en el directorio de compilación y
nunca se versiona.

Uso: python program.py --roms DIR --listados L1 [L2...] --cobertura J [J...] --cycles BIN
                        --output DIR [--por-archivo N]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generator                                  # noqa: E402
from decoder import decodificar            # noqa: E402
from generator import Emisor, NoSoportada         # noqa: E402

CABECERA = r"""/* GENERADO por tools/recomp68k/program.py: NO EDITAR, NO VERSIONAR. */
#include <string.h>
#include "cpu68k.h"
#include "cycles68k.h"
#include "dispatcher.h"

#define PD_SALTO_DIN(e)        do { c->pc = (e) & 0xFFFFFFu; return; } while (0)
#define PD_RETORNO(e)          PD_SALTO_DIN(e)
#define PD_EXCEPCION(v, pc)    do { cpu_exception(c, (v), (pc)); return; } while (0)
#define PD_SR_CAMBIADO(d)      do { c->pc = (d); return; } while (0)
#define PD_STOP(d)             do { c->pc = (d); c->stopped = 1; return; } while (0)
#define PD_RESET_EXTERNO()     ((void)0)
/* Opcode e inicio de la instrucción, para el muestreo de interrupciones (cycles68k.h). */
#define PD_INI(o)              do { c->op = (o); c->start_cycle = c->cycles; } while (0)
"""

RE_SALTO = re.compile(r"PD_(SIGUIENTE|SALTO)\(0x([0-9A-F]+)u\);")
RE_LLAMADA = re.compile(r"PD_LLAMADA\(0x([0-9A-F]+)u, 0x([0-9A-F]+)u\);")
RE_LLAMADA_DIN = re.compile(r"PD_LLAMADA_DIN\((\w+), 0x([0-9A-F]+)u\);")


class Memoria:
    def __init__(self, roms: Path):
        def cpu(b):
            a = bytearray(b)
            a[0::2], a[1::2] = b[1::2], b[0::2]
            return bytes(a)
        self.p1 = cpu((roms / "mslugx_game_m68k").read_bytes()[:1 << 20])
        self.bios = cpu((roms / "mslugx_bios_m68k").read_bytes())

    def leer16(self, d: int) -> int:
        if d + 2 <= len(self.p1):
            return int.from_bytes(self.p1[d:d + 2], "big")
        if 0xC00000 <= d and d - 0xC00000 + 2 <= len(self.bios):
            o = d - 0xC00000
            return int.from_bytes(self.bios[o:o + 2], "big")
        return 0


def leer_listados(rutas):
    instr, inputs = set(), set()
    for ruta in rutas:
        for line in Path(ruta).read_text(encoding="utf-8").splitlines():
            m = re.match(r"^; ---- funcion ([0-9A-F]{6})", line)
            if m:
                inputs.add(int(m.group(1), 16))
                continue
            m = re.match(r"^([0-9A-F]{6})", line)
            if m:
                instr.add(int(m.group(1), 16))
    return instr, inputs


def sucesores(i) -> list[int]:
    """Sucesores intraprocedurales (las llamadas continúan en la instrucción siguiente)."""
    sig = i.dir + i.largo
    if i.op in ("rts", "rte", "rtr", "jmp", "illegal", "trap", "stop"):
        return []
    if i.op == "bra":
        return [i.destino]
    if i.op in ("bcc", "dbcc"):
        return [i.destino, sig]
    if i.op in ("move_to_sr", "andi_sr", "ori_sr", "eori_sr"):
        return []                       # vuelven al despachador (pueden habilitar IRQ)
    return [sig]


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--roms", type=Path, required=True)
    ap.add_argument("--listados", type=Path, nargs="+", required=True)
    ap.add_argument("--cobertura", type=Path, nargs="*", default=[])
    ap.add_argument("--cycles", type=Path, required=True)
    ap.add_argument("--output", type=Path, required=True)
    ap.add_argument("--archivos", type=int, default=16, help="número fijo de archivos .c de output")
    ap.add_argument("--sin-punto-por-instruccion", action="store_true",
                    help="solo puntos de control en los saltos hacia atrás (las interrupciones entran "
                         "en los puntos de despacho, no en el límite exacto de la instrucción)")
    a = ap.parse_args()
    por_instruccion = not a.sin_punto_por_instruccion
    generator.cargar_ciclos(a.cycles)
    mem = Memoria(a.roms)

    instr, inputs = leer_listados(a.listados)
    for ruta in a.cobertura:
        if ruta.exists():
            for _src, dst, _banco in json.loads(ruta.read_text())["indirectos"]:
                if dst in instr:
                    inputs.add(dst)

    decod = {}
    for d in sorted(instr):
        i = decodificar(mem.leer16, d)
        if i is not None:
            decod[d] = i
    # Regiones: componentes conexos del grafo de control intraprocedural. Cada instrucción se
    # emite UNA vez, dentro de la región que la contiene; la región se entra con un switch sobre
    # c->pc, así que cualquier instrucción recompilada es un punto de entrada válido (no hace
    # falta el intérprete al volver de un punto de control a mitad de un bucle).
    padre = {d: d for d in decod}

    def raiz(x):
        while padre[x] != x:
            padre[x] = padre[padre[x]]
            x = padre[x]
        return x

    for d, i in decod.items():
        for s in sucesores(i):
            if s in decod:
                ra, rb = raiz(d), raiz(s)
                if ra != rb:
                    padre[max(ra, rb)] = min(ra, rb)
    regiones: dict[int, list[int]] = {}
    for d in sorted(decod):
        regiones.setdefault(raiz(d), []).append(d)

    a.output.mkdir(parents=True, exist_ok=True)
    for viejo in a.output.glob("program_*.c"):
        viejo.unlink()

    funciones, registros, no_soportadas = [], [], 0
    for r, miembros in sorted(regiones.items()):
        en_region = set(miembros)
        name = f"r_{r:06X}"
        casos = " ".join(f"case 0x{d:X}u: goto L_{d:06X};" for d in miembros)
        lineas = [f"static void {name}(Cpu68k *c)", "{",
                  f"    switch (c->pc) {{ {casos} default: rc_interpreter_step(c); return; }}"]
        for d in miembros:
            i = decod[d]
            try:
                cuerpo = Emisor(i).emitir()
            except NoSoportada:
                no_soportadas += 1
                cuerpo = [f"rc_interpreter_step(c); return; /* no soportada */"]
            text = "\n        ".join(cuerpo)

            def salto(m, d=d):
                destino = int(m.group(2), 16)
                if destino in en_region:
                    if destino <= d and not por_instruccion:   # hacia atrás: punto de control
                        return (f"if (c->cycles >= rc_cycle_limit) {{ c->pc = 0x{destino:X}u; return; }} "
                                f"goto L_{destino:06X};")
                    return f"goto L_{destino:06X};"
                return f"{{ c->pc = 0x{destino:X}u; return; }}"

            def continuar(ret):
                if ret in en_region:
                    return f"goto L_{ret:06X};"
                return f"{{ c->pc = 0x{ret:X}u; return; }}"

            text = RE_SALTO.sub(salto, text)
            text = RE_LLAMADA.sub(lambda m: f"if (!rc_call(c, 0x{m.group(1)}u, 0x{m.group(2)}u)) return; "
                                             + continuar(int(m.group(2), 16)), text)
            text = RE_LLAMADA_DIN.sub(lambda m: f"if (!rc_call(c, {m.group(1)}, 0x{m.group(2)}u)) return; "
                                                 + continuar(int(m.group(2), 16)), text)
            lineas.append(f"  L_{d:06X}: {{  /* {i.text()} */")
            if por_instruccion:
                # Punto de control en el límite de cada instrucción: los eventos (interrupciones,
                # líneas del video) se atienden antes de la primera instrucción que empieza en o
                # después de su ciclo, como en el 68000 real.
                lineas.append(f"        if (c->cycles >= rc_cycle_limit) {{ c->pc = 0x{d:X}u; return; }}")
            lineas.append("        " + text)
            lineas.append("    }")
        lineas.append("}")
        funciones.append("\n".join(lineas))
        registros.append((name, miembros))

    archivos = 0
    por = max(1, -(-len(funciones) // a.archivos))
    for k in range(0, a.archivos * por, por):
        bloque = funciones[k:k + por]
        registro = "\n".join(f"    for (size_t i = 0; i < sizeof d_{n} / sizeof d_{n}[0]; i++) rc_register(d_{n}[i], {n});"
                              for n, _ in registros[k:k + por])
        tablas = "\n".join(f"static const uint32_t d_{n}[] = {{{', '.join(f'0x{d:X}u' for d in ms)}}};"
                            for n, ms in registros[k:k + por])
        (a.output / f"program_{archivos:03d}.c").write_text(
            CABECERA + "\n\n".join(bloque) + "\n\n" + tablas +
            f"\n\nvoid program_register_{archivos:03d}(void)\n{{\n{registro}\n}}\n", encoding="utf-8")
        archivos += 1
    llamadas = "\n".join(f"    program_register_{n:03d}();" for n in range(archivos))
    decls = "\n".join(f"void program_register_{n:03d}(void);" for n in range(archivos))
    (a.output / "program_registry.c").write_text(
        "/* GENERADO: NO EDITAR, NO VERSIONAR. */\n" + decls +
        f"\n\nvoid program_register(void)\n{{\n{llamadas}\n}}\n", encoding="utf-8")
    print(f"regiones: {len(funciones)}  instrucciones: {len(decod)}  archivos: {archivos}  "
          f"no soportadas: {no_soportadas}  (inputs del análisis: {len(inputs)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
