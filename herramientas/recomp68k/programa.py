"""Genera el programa recompilado completo (P1 y BIOS del usuario) como funciones C.

Entradas de función:
  - inicios de función del análisis (listados de la Fase 1),
  - destinos observados de saltos indirectos (cobertura dinámica de MAME),
  - direcciones de retorno de cada JSR/BSR (continuaciones: permiten reanudar en código nativo
    cuando una llamada no vuelve "limpia").

Cada función se emite con una etiqueta por instrucción. Los saltos a instrucciones de la misma
función son `goto`; los saltos hacia atrás pasan antes por un punto de control (límite de
ciclos -> despachador) para que los bucles de espera no bloqueen las interrupciones. Lo demás
vuelve al despachador con c->pc.

La salida se deriva del programa del juego: se escribe en el directorio de compilación y
nunca se versiona.

Uso: python programa.py --roms DIR --listados L1 [L2...] --cobertura J [J...] --ciclos BIN
                        --salida DIR [--por-archivo N]
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import generador                                  # noqa: E402
from decodificador import decodificar            # noqa: E402
from generador import Emisor, NoSoportada         # noqa: E402

CABECERA = r"""/* GENERADO por herramientas/recomp68k/programa.py: NO EDITAR, NO VERSIONAR. */
#include <string.h>
#include "cpu68k.h"
#include "despachador.h"

#define PD_SALTO_DIN(e)        do { c->pc = (e) & 0xFFFFFFu; return; } while (0)
#define PD_RETORNO(e)          PD_SALTO_DIN(e)
#define PD_EXCEPCION(v, pc)    do { cpu_excepcion(c, (v), (pc)); return; } while (0)
#define PD_SR_CAMBIADO(d)      do { c->pc = (d); return; } while (0)
#define PD_STOP(d)             do { c->pc = (d); c->detenido = 1; return; } while (0)
#define PD_RESET_EXTERNO()     ((void)0)
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
    instr, entradas = set(), set()
    for ruta in rutas:
        for linea in Path(ruta).read_text(encoding="utf-8").splitlines():
            m = re.match(r"^; ---- funcion ([0-9A-F]{6})", linea)
            if m:
                entradas.add(int(m.group(1), 16))
                continue
            m = re.match(r"^([0-9A-F]{6})", linea)
            if m:
                instr.add(int(m.group(1), 16))
    return instr, entradas


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
    ap.add_argument("--ciclos", type=Path, required=True)
    ap.add_argument("--salida", type=Path, required=True)
    ap.add_argument("--archivos", type=int, default=16, help="número fijo de archivos .c de salida")
    a = ap.parse_args()
    generador.cargar_ciclos(a.ciclos)
    mem = Memoria(a.roms)

    instr, entradas = leer_listados(a.listados)
    for ruta in a.cobertura:
        if ruta.exists():
            for _src, dst, _banco in json.loads(ruta.read_text())["indirectos"]:
                if dst in instr:
                    entradas.add(dst)

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

    a.salida.mkdir(parents=True, exist_ok=True)
    for viejo in a.salida.glob("programa_*.c"):
        viejo.unlink()

    funciones, registros, no_soportadas = [], [], 0
    for r, miembros in sorted(regiones.items()):
        en_region = set(miembros)
        nombre = f"r_{r:06X}"
        casos = " ".join(f"case 0x{d:X}u: goto L_{d:06X};" for d in miembros)
        lineas = [f"static void {nombre}(Cpu68k *c)", "{",
                  f"    switch (c->pc) {{ {casos} default: rc_interprete_paso(c); return; }}"]
        for d in miembros:
            i = decod[d]
            try:
                cuerpo = Emisor(i).emitir()
            except NoSoportada:
                no_soportadas += 1
                cuerpo = [f"rc_interprete_paso(c); return; /* no soportada */"]
            texto = "\n        ".join(cuerpo)

            def salto(m, d=d):
                destino = int(m.group(2), 16)
                if destino in en_region:
                    if destino <= d:          # hacia atrás: punto de control
                        return (f"if (c->ciclos >= rc_limite_ciclos) {{ c->pc = 0x{destino:X}u; return; }} "
                                f"goto L_{destino:06X};")
                    return f"goto L_{destino:06X};"
                return f"{{ c->pc = 0x{destino:X}u; return; }}"

            def continuar(ret):
                if ret in en_region:
                    return f"goto L_{ret:06X};"
                return f"{{ c->pc = 0x{ret:X}u; return; }}"

            texto = RE_SALTO.sub(salto, texto)
            texto = RE_LLAMADA.sub(lambda m: f"if (!rc_llamar(c, 0x{m.group(1)}u, 0x{m.group(2)}u)) return; "
                                             + continuar(int(m.group(2), 16)), texto)
            texto = RE_LLAMADA_DIN.sub(lambda m: f"if (!rc_llamar(c, {m.group(1)}, 0x{m.group(2)}u)) return; "
                                                 + continuar(int(m.group(2), 16)), texto)
            lineas.append(f"  L_{d:06X}: {{  /* {i.texto()} */")
            lineas.append("        " + texto)
            lineas.append("    }")
        lineas.append("}")
        funciones.append("\n".join(lineas))
        registros.append((nombre, miembros))

    archivos = 0
    por = max(1, -(-len(funciones) // a.archivos))
    for k in range(0, a.archivos * por, por):
        bloque = funciones[k:k + por]
        registro = "\n".join(f"    for (size_t i = 0; i < sizeof d_{n} / sizeof d_{n}[0]; i++) rc_registrar(d_{n}[i], {n});"
                              for n, _ in registros[k:k + por])
        tablas = "\n".join(f"static const uint32_t d_{n}[] = {{{', '.join(f'0x{d:X}u' for d in ms)}}};"
                            for n, ms in registros[k:k + por])
        (a.salida / f"programa_{archivos:03d}.c").write_text(
            CABECERA + "\n\n".join(bloque) + "\n\n" + tablas +
            f"\n\nvoid programa_registrar_{archivos:03d}(void)\n{{\n{registro}\n}}\n", encoding="utf-8")
        archivos += 1
    llamadas = "\n".join(f"    programa_registrar_{n:03d}();" for n in range(archivos))
    decls = "\n".join(f"void programa_registrar_{n:03d}(void);" for n in range(archivos))
    (a.salida / "programa_registro.c").write_text(
        "/* GENERADO: NO EDITAR, NO VERSIONAR. */\n" + decls +
        f"\n\nvoid programa_registrar(void)\n{{\n{llamadas}\n}}\n", encoding="utf-8")
    print(f"regiones: {len(funciones)}  instrucciones: {len(decod)}  archivos: {archivos}  "
          f"no soportadas: {no_soportadas}  (entradas del análisis: {len(entradas)})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
