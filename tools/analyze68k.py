"""Análisis estático del código 68000 de mslugx (Fase 1).

Desensamblado recursivo (Capstone) sobre la vista CPU de P1 y del BIOS, con raíces en:
  - los vectors del 68000 de P1 (0x000-0x0FF),
  - las inputs que la cabecera Neo Geo expone al BIOS (JMP en 0x122/0x128/0x12E/0x134),
  - las direcciones ejecutadas que registró la cobertura dinámica (tools/coverage.py),
  - los destinos observados de saltos indirectos.

Produce:
  - <raíz>/analysis/listing_p1.txt      desensamblado anotado (fuera del repo: contiene el juego)
  - docs/functions_p1.csv                 lista de funciones (solo direcciones y métricas)
  - docs/PHASE1_COVERAGE.md              reporte de cobertura

Uso:
    python tools/analyze68k.py [--cobertura analisis/coverage.json]
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
TERMINA = {"rts", "rte", "rtr", "jmp", "bra", "illegal", "stop"}
HEURISTICAS = {"puntero_en_codigo", "tabla_punteros"}
SALTOS_COND = {"bra", "bhi", "bls", "bcc", "bcs", "bne", "beq", "bvc", "bvs",
               "bpl", "bmi", "bge", "blt", "bgt", "ble", "bhs", "blo"}


def base_mn(i) -> str:
    """Mnemónico sin sufijo de tamaño: 'bra.s' -> 'bra', 'btst.l' -> 'btst'."""
    return i.mnemonic.split(".")[0]
VECTORES = {0: "SSP", 1: "RESET", 2: "BUS_ERROR", 3: "ADDRESS_ERROR", 4: "ILLEGAL", 5: "ZERO_DIVIDE",
            6: "CHK", 7: "TRAPV", 8: "PRIVILEGE", 9: "TRACE", 10: "LINE_A", 11: "LINE_F",
            24: "SPURIOUS", 25: "IRQ1_VBLANK", 26: "IRQ2_TIMER", 27: "IRQ3", 28: "IRQ4", 29: "IRQ5",
            30: "IRQ6", 31: "IRQ7"}
CABECERA = {0x122: "USER", 0x128: "PLAYER_START", 0x12E: "DEMO_END", 0x134: "COIN_SOUND"}


def vista_cpu(b: bytes) -> bytes:
    """Archivos .p / .sp1 (orden de MAME) -> memoria big-endian como la ve el 68000."""
    a = bytearray(b)
    a[0::2], a[1::2] = b[1::2], b[0::2]
    return bytes(a)


class Memoria:
    def __init__(self, roms: Path) -> None:
        p = vista_cpu((roms / "mslugx_game_m68k").read_bytes())
        self.regiones = [(0x000000, p[:MB], "p1"),
                         (0xC00000, vista_cpu((roms / "mslugx_bios_m68k").read_bytes()), "bios")]

    def region(self, addr: int):
        for base, data, name in self.regiones:
            if base <= addr < base + len(data):
                return base, data, name
        return None

    def u32(self, addr: int) -> int:
        base, d, _ = self.region(addr)
        o = addr - base
        return int.from_bytes(d[o:o + 4], "big")


class Analizador:
    def __init__(self, mem: Memoria) -> None:
        self.mem = mem
        self.cs = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
        self.cs.detail = True
        self.ins: dict[int, object] = {}            # dirección -> instrucción
        self.sucesores: dict[int, list[int]] = {}   # flujo intraprocedural
        self.llamadas: dict[int, list[int]] = defaultdict(list)
        self.inputs: dict[int, set[str]] = defaultdict(set)
        self.indirectos_sin_resolver: set[int] = set()
        self.refs_datos: Counter = Counter()
        self.invalidos: set[int] = set()
        self.origen: dict[int, str] = {}           # dirección -> motivo de la raíz que la alcanzó
        self.datos_prohibidos: set[int] = set()    # palabras que MAME leyó como data
        self.cortes_por_datos = 0

    def decodificar(self, addr: int):
        r = self.mem.region(addr)
        if r is None or addr & 1:
            return None
        base, d, _ = r
        o = addr - base
        for i in self.cs.disasm(d[o:o + 10], addr, count=1):
            return None if i.mnemonic.startswith("dc.") else i   # dc.w = opcode inválido
        return None

    def destino(self, i) -> int | None:
        """Destino estático de un salto/llamada, o None si es indirecto."""
        for op in i.operands:
            if op.type == M.M68K_OP_BR_DISP:
                return i.address + 2 + op.br_disp.disp
            if op.type == M.M68K_OP_MEM:
                am = op.address_mode
                if am in (M.M68K_AM_ABSOLUTE_DATA_LONG, M.M68K_AM_ABSOLUTE_DATA_SHORT):
                    return op.imm & 0xFFFFFF if op.imm else self._abs_de_texto(i)
                if am == M.M68K_AM_PCI_DISP:
                    return i.address + 2 + op.mem.disp
                return None
        return self._abs_de_texto(i)

    @staticmethod
    def _abs_de_texto(i) -> int | None:
        m = re.search(r"\$([0-9a-f]+)", i.op_str)
        return int(m.group(1), 16) & 0xFFFFFF if m and "(" not in i.op_str else None

    def explorar(self, raices: dict[int, str], indirectos_obs: dict[int, set[int]]) -> None:
        pendientes = []
        for a, motivo in raices.items():
            if motivo != "dinamica":          # lo ejecutado es raíz de exploración, no inicio de función
                self.inputs[a].add(motivo)
            pendientes.append((a, motivo))
        while pendientes:
            a, motivo = pendientes.pop()
            while a not in self.ins:
                i = self.decodificar(a)
                if i is None:
                    self.invalidos.add(a)
                    break
                if motivo in HEURISTICAS and any(o in self.datos_prohibidos
                                                 for o in range(a & ~1, a + i.size, 2)):
                    self.cortes_por_datos += 1        # la heurística pisaría data demostrados
                    break
                self.ins[a] = i
                self.origen[a] = motivo
                mn = base_mn(i)
                sig = a + i.size
                suc = []
                self._refs(i)
                if mn in ("bsr", "jsr"):
                    t = self.destino(i)
                    objetivos = [t] if t is not None else sorted(indirectos_obs.get(a, ()))
                    if t is None and not objetivos:
                        self.indirectos_sin_resolver.add(a)
                    for t in objetivos:
                        self.llamadas[a].append(t)
                        self.inputs[t].add("llamada")
                        pendientes.append((t, motivo))
                    suc.append(sig)
                elif mn == "jmp" or mn in SALTOS_COND or mn.startswith("db"):
                    t = self.destino(i)
                    objetivos = [t] if t is not None else sorted(indirectos_obs.get(a, ()))
                    if t is None and not objetivos:
                        self.indirectos_sin_resolver.add(a)
                    for t in objetivos:
                        suc.append(t)
                        pendientes.append((t, motivo))
                    if mn not in TERMINA:
                        suc.append(sig)
                elif mn not in TERMINA:
                    suc.append(sig)
                self.sucesores[a] = suc
                if mn in TERMINA or not suc or suc[-1] != sig:
                    break
                a = sig

    def _refs(self, i) -> None:
        """Referencias absolutas a data en P1 (lea/move/etc. con direcciones absolutas)."""
        mn = base_mn(i)
        if mn in ("jmp", "jsr", "bsr") or mn in SALTOS_COND or mn.startswith("db"):
            return
        for m in re.finditer(r"\$([0-9a-f]{4,6})\.l", i.op_str):
            v = int(m.group(1), 16)
            if v < MB:
                self.refs_datos[v] += 1
        for op in i.operands:
            if op.type == M.M68K_OP_MEM and op.address_mode == M.M68K_AM_PCI_DISP:
                self.refs_datos[i.address + 2 + op.mem.disp] += 1

    # ---------------------------------------------------------------- heurísticas
    def parece_codigo(self, addr: int, max_ins: int = 64) -> bool:
        """Candidato válido: dirección par en P1 que decodifica sin opcodes inválidos hasta un
        terminador, sin caer a mitad de una instrucción ya conocida y sin empezar en relleno."""
        if addr & 1 or not (0x200 <= addr < MB):
            return False
        if addr in self.ins:
            return True
        p1 = self.mem.regiones[0][1]
        if p1[addr:addr + 4] in (b"\x00\x00\x00\x00", b"\xff\xff\xff\xff"):
            return False
        a = addr
        for _ in range(max_ins):
            if a in self.ins:
                return True                      # se une a código conocido en un límite de instrucción
            if self.dentro_de_instruccion(a):
                return False
            i = self.decodificar(a)
            if i is None or any(o in self.datos_prohibidos for o in range(a & ~1, a + i.size, 2)):
                return False
            mn = base_mn(i)
            if mn in ("rts", "rte", "rtr", "jmp", "bra"):
                return True
            a += i.size
        return False

    def dentro_de_instruccion(self, a: int) -> bool:
        for d in (2, 4, 6, 8):
            i = self.ins.get(a - d)
            if i is not None and i.size > d:
                return True
        return False

    def punteros_en_codigo(self) -> set[int]:
        """Constantes en el código confirmado que apuntan a P1 (move.l #imm / lea / pea abs)."""
        cand = set()
        for i in list(self.ins.values()):
            if i.address >= MB:
                continue
            for m in re.finditer(r"#\$([0-9a-f]{3,6})\b|\$([0-9a-f]{3,6})\.l", i.op_str):
                v = int(m.group(1) or m.group(2), 16)
                if v < MB and v not in self.ins and self.parece_codigo(v):
                    cand.add(v)
        return cand

    def tablas_de_punteros(self, minimo: int = 3) -> set[int]:
        """Tramos de >= `minimo` longwords consecutivos, fuera del código, que apuntan a código válido."""
        p1 = self.mem.regiones[0][1]
        cubierto = bytearray(MB)
        for a, i in self.ins.items():
            if a < MB:
                cubierto[a:a + i.size] = b"\x01" * i.size
        cand, racha = set(), []
        o = 0
        while o + 4 <= MB:
            v = int.from_bytes(p1[o:o + 4], "big")
            if not cubierto[o] and v < MB and self.parece_codigo(v):
                racha.append(v)
                o += 4
                continue
            if len(racha) >= minimo:
                cand.update(x for x in racha if x not in self.ins)
            racha = []
            o += 2
        if len(racha) >= minimo:
            cand.update(x for x in racha if x not in self.ins)
        return cand

    def explorar_con_heuristicas(self, raices, indirectos_obs, usar_heuristicas: bool) -> None:
        self.explorar(raices, indirectos_obs)
        if not usar_heuristicas:
            return
        while True:                                   # A: punteros en código, hasta punto fijo
            nuevos = self.punteros_en_codigo()
            if not nuevos:
                break
            self.explorar({v: "puntero_en_codigo" for v in nuevos}, indirectos_obs)
        while True:                                   # B: tablas de punteros (y lo que destapen)
            nuevos = self.tablas_de_punteros()
            if not nuevos:
                break
            self.explorar({v: "tabla_punteros" for v in nuevos}, indirectos_obs)
            while True:
                n2 = self.punteros_en_codigo()
                if not n2:
                    break
                self.explorar({v: "puntero_en_codigo" for v in n2}, indirectos_obs)

    # ---------------------------------------------------------------- funciones
    def funciones(self, desde: int = 0, hasta: int = MB) -> list[dict]:
        out = []
        for e in sorted(x for x in self.inputs if x in self.ins and desde <= x < hasta):
            visto, pila = set(), [e]
            while pila:
                a = pila.pop()
                if a in visto or a not in self.ins:
                    continue
                visto.add(a)
                pila.extend(self.sucesores.get(a, ()))
            tamaño = sum(self.ins[a].size for a in visto)
            out.append({"inicio": e, "fin": max(a + self.ins[a].size for a in visto),
                        "bytes_codigo": tamaño, "instrucciones": len(visto),
                        "llamadas": sum(len(self.llamadas.get(a, ())) for a in visto),
                        "motivos": sorted(self.inputs[e])})
        return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    raiz = Path(os.environ.get("PD_ROOT", Path(__file__).resolve().parents[2]))
    repo = Path(__file__).resolve().parents[1]
    ap.add_argument("--roms", type=Path, default=raiz / "roms")
    ap.add_argument("--cobertura", type=Path, nargs="+", default=[raiz / "analisis" / "coverage.json"],
                    help="uno o más JSON de cobertura (tools/coverage.py); se combinan")
    a = ap.parse_args()

    mem = Memoria(a.roms)
    p1 = mem.regiones[0][1]
    cov = {"pcs": {}, "indirectos": [], "lecturas_p1": [], "instrucciones_trazadas": 0}
    for ruta in a.cobertura:
        if not ruta.exists():
            continue
        parte = json.loads(ruta.read_text())
        for region, pcs in parte["pcs"].items():
            cov["pcs"][region] = sorted(set(cov["pcs"].get(region, [])) | set(pcs))
        cov["indirectos"] = sorted({tuple(x) for x in cov["indirectos"]} | {tuple(x) for x in parte["indirectos"]})
        cov["lecturas_p1"] = sorted(set(cov["lecturas_p1"]) | set(parte.get("lecturas_p1", [])))
        cov["instrucciones_trazadas"] += parte.get("instrucciones_trazadas", 0)
    dyn_p1 = set(cov["pcs"].get("p1", []))
    dyn_bios = set(cov["pcs"].get("bios", []))
    indirectos_obs: dict[int, set[int]] = defaultdict(set)
    for src, dst, _banco in cov["indirectos"]:
        indirectos_obs[src].add(dst)

    raices: dict[int, str] = {}
    for n, name in VECTORES.items():
        if n == 0:
            continue
        v = mem.u32(n * 4)
        if v < MB:
            raices[v] = f"vector_{name}"
    for off, name in CABECERA.items():
        raices.setdefault(off, f"cabecera_{name}")
    raices_estaticas = dict(raices)
    for pc in dyn_p1 | dyn_bios:
        raices.setdefault(pc, "dinamica")

    # 1) Validación: solo análisis estático (sin MAME) frente a lo que MAME ejecutó.
    solo = Analizador(mem)
    solo.explorar_con_heuristicas(raices_estaticas, {}, usar_heuristicas=True)
    recall = (sum(1 for pc in dyn_p1 if pc in solo.ins) / len(dyn_p1)) if dyn_p1 else 0.0
    solo_sin_heur = Analizador(mem)
    solo_sin_heur.explorar(raices_estaticas, {})
    recall_sin_heur = (sum(1 for pc in dyn_p1 if pc in solo_sin_heur.ins) / len(dyn_p1)) if dyn_p1 else 0.0

    # Verdad de MAME: bytes leídos como data. Son las lecturas sobre P1 menos los bytes de
    # instrucciones ejecutadas, con una holgura de 4 bytes tras cada una (precarga del 68000).
    datos_leidos: set[int] = set()
    if cov.get("lecturas_p1"):
        ejecutado = set()
        for pc in dyn_p1:
            i = solo.decodificar(pc)
            if i is not None:
                ejecutado.update(range(pc, pc + i.size + 4))
        datos_leidos = {w for w in cov["lecturas_p1"] if w not in ejecutado and w + 1 not in ejecutado}

    # 2) Análisis combinado: raíces + cobertura dinámica + heurísticas que no pisen esos data.
    an = Analizador(mem)
    an.datos_prohibidos = datos_leidos
    an.explorar_con_heuristicas(raices, indirectos_obs, usar_heuristicas=True)

    # ------------------------------------------------ cobertura de P1
    codigo = bytearray(MB)
    por_evidencia: Counter = Counter()
    for addr, i in an.ins.items():
        if addr < MB:
            codigo[addr:addr + i.size] = b"\x01" * i.size
            o = an.origen.get(addr, "?")
            clase = o if o in ("puntero_en_codigo", "tabla_punteros") else "confirmado"
            por_evidencia[clase] += i.size
    est_bytes = sum(codigo)
    dyn_bytes = sum(an.ins[p].size for p in dyn_p1 if p in an.ins)
    ref_dentro = sum(1 for r in an.refs_datos if r < MB and not codigo[r])
    # relleno: tramos de 0x00 o 0xFF de 16+ bytes fuera del código
    relleno = 0
    for m in re.finditer(rb"(\x00{16,}|\xff{16,})", p1):
        relleno += sum(1 for o in range(m.start(), m.end()) if not codigo[o])
    datos_o_desconocido = MB - est_bytes - relleno

    def falsos_positivos(instrucciones) -> int:
        """Bytes de instrucciones que MAME demostró que son data (en palabras de 2 bytes)."""
        return 2 * sum(1 for a_, n in instrucciones for o in range(a_ & ~1, a_ + n, 2) if o in datos_leidos)

    fp = Counter()
    for addr, i in an.ins.items():
        if addr < MB:
            o = an.origen.get(addr, "?")
            clase = o if o in ("puntero_en_codigo", "tabla_punteros") else "confirmado"
            fp[clase] += falsos_positivos([(addr, i.size)])

    # ------------------------------------------------ verificación cruzada con Ghidra
    ghidra_txt = "(no se ejecutó Ghidra; ver tools/ghidra/)"
    gcsv = raiz / "analisis" / "ghidra" / "instrucciones_ghidra.csv"
    if gcsv.exists():
        g_ins = {}
        for line in gcsv.read_text().split():
            d, n = line.split(",")
            g_ins[int(d, 16)] = int(n)
        g_bytes = sum(g_ins.values())
        g_recall = sum(1 for pc in dyn_p1 if pc in g_ins) / len(dyn_p1) if dyn_p1 else 0
        nuestras = {a_ for a_ in an.ins if a_ < MB}
        ambos = nuestras & set(g_ins)
        g_cubre = bytearray(MB)
        for d, n in g_ins.items():
            g_cubre[d:d + n] = b"\x01" * n
        conflicto = sum(1 for a_ in nuestras if a_ not in g_ins and g_cubre[a_])
        g_fp = falsos_positivos(g_ins.items())
        ghidra_txt = f"""Ghidra 12.1.4 headless, P1 como binario raw de 68000 big-endian, solo con las raíces
estáticas (vectors y cabecera) y su análisis automático (`tools/ghidra/`).

| Medida | Este análisis (combinado) | Ghidra |
|---|---:|---:|
| Bytes de instrucciones | {est_bytes:,} | {g_bytes:,} |
| Direcciones ejecutadas en MAME que reconoce | {100 * sum(1 for pc in dyn_p1 if pc in an.ins) / max(1, len(dyn_p1)):.1f} % | {100 * g_recall:.1f} % |
| Bytes marcados como código que MAME leyó como data (falsos positivos seguros) | {sum(fp.values()):,} | {g_fp:,} |

- Inicios de instrucción en común: {len(ambos):,}.
- Instrucciones nuestras que caen a mitad de una instrucción de Ghidra (desalineación; uno
  de los dos se equivoca): {conflicto:,}.
- Sin data de MAME, este análisis reconoce el {100 * recall:.1f} % de lo ejecutado; Ghidra,
  también sin MAME, el {100 * g_recall:.1f} %."""

    funcs = an.funciones()
    llamadas_bios = Counter(t for src, ts in an.llamadas.items() if src < MB for t in ts if t >= 0xC00000)
    saltos_bios = Counter(an.destino(i) for a_, i in an.ins.items()
                          if a_ < MB and base_mn(i) == "jmp" and (an.destino(i) or 0) >= 0xC00000)
    sin_res_p1 = sorted(x for x in an.indirectos_sin_resolver if x < MB)

    # ------------------------------------------------ salidas
    listado = raiz / "analisis" / "listing_p1.txt"
    listado.parent.mkdir(parents=True, exist_ok=True)
    inicios = {f["inicio"] for f in funcs}
    with listado.open("w", encoding="utf-8") as f:
        for addr in sorted(x for x in an.ins if x < MB):
            if addr in inicios:
                f.write(f"\n; ---- funcion {addr:06X}  ({', '.join(sorted(an.inputs[addr]))})\n")
            i = an.ins[addr]
            marca = "*" if addr in dyn_p1 else " "
            f.write(f"{addr:06X}{marca} {i.mnemonic:<8} {i.op_str}\n")

    # Listado del BIOS (código alcanzado desde lo que se ejecutó en MAME), mismo formato.
    funcs_bios = {fn["inicio"] for fn in an.funciones(0xC00000, 0xC20000)}
    with (raiz / "analisis" / "listing_bios.txt").open("w", encoding="utf-8") as f:
        for addr in sorted(x for x in an.ins if 0xC00000 <= x < 0xC20000):
            if addr in funcs_bios:
                f.write(f"\n; ---- funcion {addr:06X}  ({', '.join(sorted(an.inputs[addr]))})\n")
            i = an.ins[addr]
            marca = "*" if addr in dyn_bios else " "
            f.write(f"{addr:06X}{marca} {i.mnemonic:<8} {i.op_str}\n")

    with (repo / "docs" / "functions_p1.csv").open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["inicio", "fin", "bytes_codigo", "instrucciones", "llamadas", "motivos"])
        for fn in funcs:
            w.writerow([f"{fn['inicio']:06X}", f"{fn['fin']:06X}", fn["bytes_codigo"], fn["instrucciones"],
                        fn["llamadas"], " ".join(fn["motivos"])])

    vec_txt = "\n".join(f"| {n} | `{n * 4:#05x}` | {name} | `{mem.u32(n * 4):06X}` |"
                        for n, name in VECTORES.items())
    cab_txt = "\n".join(f"| `{off:#05x}` | {name} | `{an.ins[off].mnemonic} {an.ins[off].op_str}` |"
                        if off in an.ins else f"| `{off:#05x}` | {name} | (no decodifica) |"
                        for off, name in CABECERA.items())
    bios_txt = "\n".join(f"| `{t:06X}` | {n} | {saltos_bios.get(t, 0)} |"
                         for t, n in sorted((llamadas_bios + Counter()).items()))
    for t, n in saltos_bios.items():
        if t not in llamadas_bios:
            bios_txt += f"\n| `{t:06X}` | 0 | {n} |"
    regiones = ", ".join(f"{k}: {len(v)}" for k, v in sorted(cov["pcs"].items()))
    no_cubiertos = sorted(p for p in dyn_p1 if p not in an.ins)

    reporte = f"""# Fase 1: reporte de cobertura del análisis del 68000

Generado por `tools/analyze68k.py` (Capstone {capstone.__version__}) a partir de los ROMs del
usuario y de la cobertura dinámica de MAME (`tools/coverage.py`,
{cov.get('instrucciones_trazadas', 0):,} instrucciones trazadas).

## P1 (`0x000000`–`0x0FFFFF`, 1 MB)

| Medida | Bytes | % |
|---|---:|---:|
| Código (total) | {est_bytes:,} | {100 * est_bytes / MB:.1f} |
| ...confirmado: alcanzado desde vectors, cabecera, llamadas directas o ejecución en MAME | {por_evidencia['confirmado']:,} | {100 * por_evidencia['confirmado'] / MB:.1f} |
| ...heurística A: puntero cargado como constante por código confirmado | {por_evidencia['puntero_en_codigo']:,} | {100 * por_evidencia['puntero_en_codigo'] / MB:.1f} |
| ...heurística B: tabla de 3 o más punteros a código válido | {por_evidencia['tabla_punteros']:,} | {100 * por_evidencia['tabla_punteros'] / MB:.1f} |
| ...del total, ejecutado en MAME (cobertura dinámica) | {dyn_bytes:,} | {100 * dyn_bytes / MB:.1f} |
| Relleno (tramos de 0x00 o 0xFF de 16 bytes o más fuera del código) | {relleno:,} | {100 * relleno / MB:.1f} |
| Datos o desconocido | {datos_o_desconocido:,} | {100 * datos_o_desconocido / MB:.1f} |

- Funciones detectadas: **{len(funcs)}** (lista completa en `docs/functions_p1.csv`).
- **Validación de las heurísticas contra MAME:** de las {len(dyn_p1):,} direcciones de P1 que
  MAME ejecutó, el análisis puramente estático (sin data de MAME) encuentra el
  **{100 * recall:.1f} %** con heurísticas y el {100 * recall_sin_heur:.1f} % sin ellas.
- Instrucciones decodificadas: {sum(1 for x in an.ins if x < MB):,}.
- Direcciones de data referenciadas fuera del código: {ref_dentro:,}.
- Saltos o llamadas indirectos en P1 **sin resolver** (sin destino observado en MAME):
  {len(sin_res_p1)}. Primeros: {', '.join(f'`{x:06X}`' for x in sin_res_p1[:15])}
- Direcciones ejecutadas en MAME que el análisis estático no alcanza: {len(no_cubiertos)}
  (deben ser 0; si no, hay un fallo de decodificación).
- Direcciones de arranque inválidas (no decodifican): {len([x for x in an.invalidos if x < MB])}.

## Precisión: código que MAME demostró que es dato

MAME registró {len(datos_leidos):,} palabras de P1 leídas como data, y no como instrucciones.
Bytes marcados como código que caen sobre esas palabras, según la evidencia:

| Evidencia | Bytes de código | Falsos positivos seguros |
|---|---:|---:|
| Confirmado | {por_evidencia['confirmado']:,} | {fp['confirmado']:,} |
| Heurística A (puntero en código) | {por_evidencia['puntero_en_codigo']:,} | {fp['puntero_en_codigo']:,} |
| Heurística B (tabla de punteros) | {por_evidencia['tabla_punteros']:,} | {fp['tabla_punteros']:,} |

Es una cota inferior: solo detecta los data que el juego leyó durante la traza. En el análisis
combinado, las rutas heurísticas que pisarían esos data se cortan ({an.cortes_por_datos} cortes),
así que aquí deberían quedar en 0. La validación puramente estática no usa esta información.

## Verificación cruzada con Ghidra

{ghidra_txt}

## Cobertura dinámica por región (direcciones de instrucción distintas)

{regiones}

## Vectores del 68000 (en P1)

| # | Offset | Vector | Destino |
|---|---|---|---|
{vec_txt}

## Entradas de la cabecera Neo Geo

| Offset | Entrada | Instrucción |
|---|---|---|
{cab_txt}

## Llamadas y saltos al BIOS (`0xC00000`–`0xC0FFFF`)

| Destino | Llamadas (jsr/bsr) | Saltos (jmp) |
|---|---:|---:|
{bios_txt}
"""
    (repo / "docs" / "PHASE1_COVERAGE.md").write_text(reporte, encoding="utf-8")
    print(reporte)
    print(f"Listado: {listado}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
