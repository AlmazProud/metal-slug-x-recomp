"""Colas de muestreo de interrupciones (y colas efectivas) del 68000, a partir de la descripción del microcódigo que
MAME usa como documentación (src/devices/cpu/m68000/m68000-sdf.cpp, no se copia código).

Para cada manejador de instrucción recorre todos los caminos y mide cuántos ciclos quedan desde
el último paso en que el 68000 decide si atiende una interrupción
("m_next_state = m_int_next_state") hasta el final de la instrucción. Ignora los bloques de
suspensión ("if(m_icount <= 0) {...}") y de error de dirección, que no son el camino normal.

Cola efectiva: ciclos desde el último fin de acceso al bus anterior (o igual) al muestreo hasta
el final; None si no hay acceso antes del muestreo dentro de la instrucción (el último acceso
es el final de la instrucción anterior). MAME solo procesa una IRQ activada en T cuando la CPU
termina un acceso con T <= fin; los muestreos anteriores a ese punto no la ven.

Salida por camino: (duración, L, U), medidos desde el inicio de la instrucción: L = último fin de
acceso anterior o igual al muestreo (None: ninguno, vale el de la instrucción anterior) y U = último fin de
acceso de la instrucción.

Uso: python colas_muestreo.py RUTA/m68000-sdf.cpp
Imprime un patrón (valor/máscara) por manejador con su conjunto de colas, para escribir las
reglas por familia de src/cpu/ciclos68k.h.
"""
from __future__ import annotations

import re
import sys
from collections import defaultdict


def bloques(lineas: list[str]) -> list[str]:
    """Quita los bloques condicionales de suspensión y de error de dirección."""
    out, i = [], 0
    while i < len(lineas):
        l = lineas[i].strip()
        if l.startswith("if(m_icount <= 0) {") or l.startswith("if(m_aob & 1) {"):
            prof = l.count("{") - l.count("}")
            i += 1
            while i < len(lineas) and prof > 0:
                prof += lineas[i].count("{") - lineas[i].count("}")
                i += 1
            continue
        out.append(l)
        i += 1
    return out


def caminos(lineas: list[str], inicio: int, etiquetas: dict[str, int], muestreo: int | None,
            ciclos: int, visitas: int, res: set, total: int = 0, acceso: int | None = None,
            efectiva: int | None = None):
    if visitas > 12:                 # bucles de microcódigo (MOVEM, desplazamientos): basta con unos caminos
        return
    i = inicio
    while i < len(lineas):
        l = lineas[i]
        if "m_next_state = m_int_next_state" in l:
            muestreo, ciclos = 0, 0
            efectiva = total - acceso if acceso is not None else None
        m = re.search(r"m_icount -= (\d+);", l)
        if m:
            ciclos += int(m.group(1))
            total += int(m.group(1))
            if any("_interruptible(" in x or ".read" in x or ".write" in x for x in lineas[max(0, i - 3):i]):
                acceso = total
        if l.startswith("m_inst_state = m_next_state ?") or (l == "return;" and i == len(lineas) - 1):
            if muestreo is not None:
                res.add((total, None if efectiva is None else total - (efectiva + ciclos), acceso))
            return
        m = re.match(r"goto (\w+);", l)
        if m:
            if visitas > 20:
                return
            caminos(lineas, etiquetas[m.group(1)] + 1, etiquetas, muestreo, ciclos, visitas + 1, res, total, acceso, efectiva)
            return
        if l.startswith("if(m_t)") or l.startswith("if(!m_t)") or re.match(r"if\(.*\)$", l):
            # bifurcación "if(cond) goto A; else goto B;"
            sig = lineas[i + 1] if i + 1 < len(lineas) else ""
            mm = re.match(r"goto (\w+);", sig)
            if mm and i + 3 < len(lineas) and lineas[i + 2] == "else":
                mm2 = re.match(r"goto (\w+);", lineas[i + 3])
                caminos(lineas, etiquetas[mm.group(1)] + 1, etiquetas, muestreo, ciclos, visitas + 1, res, total, acceso, efectiva)
                if mm2:
                    caminos(lineas, etiquetas[mm2.group(1)] + 1, etiquetas, muestreo, ciclos, visitas + 1, res, total, acceso, efectiva)
                return
        i += 1
    if muestreo is not None:
        res.add((total, None if efectiva is None else total - (efectiva + ciclos), acceso))


def main() -> int:
    src = open(sys.argv[1], encoding="utf-8").read()
    for f in re.split(r"\nvoid m68000_device::", src)[1:]:
        m = re.match(r"(\w+)_df\(\)\s*//\s*([0-9a-f]{4})\s+([0-9a-f]{4})", f)
        if not m:
            continue
        cuerpo = f.split("\n", 1)[1]
        cuerpo = cuerpo[:cuerpo.rfind("}")]
        lineas = [l for l in bloques(cuerpo.splitlines()) if l and not l.startswith("//")]
        etiquetas = {l[:-1]: k for k, l in enumerate(lineas) if re.fullmatch(r"\w+:", l)}
        res: set = set()
        caminos(lineas, 0, etiquetas, None, 0, 0, res)
        print(f"{m.group(1):<28} {m.group(2).upper()}/{m.group(3).upper()} {sorted(res, key=str)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
