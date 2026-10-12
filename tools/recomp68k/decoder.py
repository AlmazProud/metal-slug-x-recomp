"""Decodificador del Motorola 68000 (solo 68000) para el recompilador.

Decodifica según la codificación del M68000 Programmer's Reference Manual. Es independiente de
Capstone, que en modo 68000 interpreta algunas palabras de extensión con el formato completo
del 68020. Se valida contra Capstone (longitudes y mnemónicos) y contra Musashi (semántica) en
las pruebas de tests/.

API:
    decodificar(leer16, direccion) -> Instr | None
donde leer16(dir) devuelve la palabra big-endian en esa dirección.
"""
from __future__ import annotations

from dataclasses import dataclass, field

CONDICIONES = ["t", "f", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"]
TAMAÑO_BYTES = {"b": 1, "w": 2, "l": 4}


@dataclass
class EA:
    """Operando efectivo (modo de direccionamiento) ya decodificado."""
    modo: str                  # Dn An (An) (An)+ -(An) d16(An) d8(An,Xi) abs.w abs.l d16(PC) d8(PC,Xi) #imm
    reg: int = 0               # número de registro (0-7) para los modos con registro
    disp: int = 0              # desplazamiento con signo (d16 / d8)
    xi: int = 0                # registro índice: 0-7 = D0-D7, 8-15 = A0-A7
    xi_largo: bool = False     # índice .l (True) o .w con extensión de signo (False)
    valor: int = 0             # dirección absoluta o valor inmediato (sin signo, ya recortado)
    pc_ext: int = 0            # dirección de la palabra de extensión (base de los modos PC)

    def es_memoria(self) -> bool:
        return self.modo not in ("Dn", "An", "#imm")

    def texto(self) -> str:
        x = ("a" if self.xi >= 8 else "d") + str(self.xi & 7) + (".l" if self.xi_largo else ".w")
        return {
            "Dn": f"d{self.reg}", "An": f"a{self.reg}", "(An)": f"(a{self.reg})",
            "(An)+": f"(a{self.reg})+", "-(An)": f"-(a{self.reg})", "d16(An)": f"{self.disp}(a{self.reg})",
            "d8(An,Xi)": f"{self.disp}(a{self.reg},{x})", "abs.w": f"${self.valor:x}.w",
            "abs.l": f"${self.valor:x}.l", "d16(PC)": f"${self.pc_ext + self.disp:x}(pc)",
            "d8(PC,Xi)": f"${self.pc_ext + self.disp:x}(pc,{x})", "#imm": f"#${self.valor:x}",
        }[self.modo]


@dataclass
class Instr:
    dir: int
    largo: int                 # bytes totales
    op: str                    # nombre canónico: move, add, bcc, dbcc, ...
    tam: str | None = None     # b / w / l
    ops: list = field(default_factory=list)   # operandos EA (fuente primero)
    cc: str | None = None      # condición de Bcc/Scc/DBcc
    destino: int | None = None # destino de saltos relativos (Bcc/BSR/DBcc)
    lista: int = 0             # máscara de MOVEM (bit 0 = D0 ... bit 15 = A7, sin invertir)
    dato: int = 0              # dato adicional: cuenta de shift, vector de TRAP, desplazamiento de LINK
    opcode: int = 0

    def texto(self) -> str:
        n = self.op + (self.cc or "") + (f".{self.tam}" if self.tam else "")
        partes = [e.texto() for e in self.ops]
        if self.destino is not None:
            partes.append(f"${self.destino:x}")
        return f"{n} {', '.join(partes)}".strip()


class _Lector:
    def __init__(self, leer16, dir_):
        self.leer16 = leer16
        self.inicio = dir_
        self.pos = dir_ + 2

    def w(self) -> int:
        v = self.leer16(self.pos) & 0xFFFF
        self.pos += 2
        return v

    def l(self) -> int:
        alto = self.w()
        return (alto << 16) | self.w()


def _s8(v): return v - 0x100 if v & 0x80 else v
def _s16(v): return v - 0x10000 if v & 0x8000 else v


def _ea(rd: _Lector, modo: int, reg: int, tam: str | None) -> EA | None:
    """Decodifica un EA (modo, registro) leyendo sus palabras de extensión."""
    if modo == 0:
        return EA("Dn", reg)
    if modo == 1:
        return EA("An", reg)
    if modo == 2:
        return EA("(An)", reg)
    if modo == 3:
        return EA("(An)+", reg)
    if modo == 4:
        return EA("-(An)", reg)
    if modo == 5:
        return EA("d16(An)", reg, disp=_s16(rd.w()))
    if modo == 6:
        ext = rd.w()           # formato breve: en el 68000 se ignoran los bits 8-10
        return EA("d8(An,Xi)", reg, disp=_s8(ext & 0xFF), xi=ext >> 12, xi_largo=bool(ext & 0x800))
    if modo == 7:
        if reg == 0:
            return EA("abs.w", valor=_s16(rd.w()) & 0xFFFFFFFF)
        if reg == 1:
            return EA("abs.l", valor=rd.l())
        if reg == 2:
            base = rd.pos
            return EA("d16(PC)", disp=_s16(rd.w()), pc_ext=base)
        if reg == 3:
            base = rd.pos
            ext = rd.w()
            return EA("d8(PC,Xi)", disp=_s8(ext & 0xFF), xi=ext >> 12, xi_largo=bool(ext & 0x800), pc_ext=base)
        if reg == 4 and tam is not None:
            if tam == "l":
                return EA("#imm", valor=rd.l())
            v = rd.w()
            return EA("#imm", valor=(v & 0xFF) if tam == "b" else v)
    return None


# Clases de EA válidas para cada operación (se rechaza lo que el 68000 no admite).
_DATOS_ALTERABLE = {"Dn", "(An)", "(An)+", "-(An)", "d16(An)", "d8(An,Xi)", "abs.w", "abs.l"}
_MEM_ALTERABLE = _DATOS_ALTERABLE - {"Dn"}
_TODOS = _DATOS_ALTERABLE | {"An", "d16(PC)", "d8(PC,Xi)", "#imm"}
_DATOS = _TODOS - {"An"}
_CONTROL = {"(An)", "d16(An)", "d8(An,Xi)", "abs.w", "abs.l", "d16(PC)", "d8(PC,Xi)"}
_ALTERABLE = _DATOS_ALTERABLE | {"An"}


def _tam2(bits):  # tamaños en bits 7-6: 00=b 01=w 10=l
    return {0: "b", 1: "w", 2: "l"}.get(bits)


def decodificar(leer16, dir_: int) -> Instr | None:
    op = leer16(dir_) & 0xFFFF
    rd = _Lector(leer16, dir_)
    i = _decodificar(op, rd, dir_)
    if i is not None:
        i.largo = rd.pos - dir_
        i.opcode = op
    return i


def _ok(ea, clase):
    return ea is not None and ea.modo in clase


def _decodificar(op: int, rd: _Lector, dir_: int) -> Instr | None:  # noqa: C901 (tabla de decodificación)
    g = op >> 12
    modo, reg = (op >> 3) & 7, op & 7
    reg9, modo6 = (op >> 9) & 7, (op >> 6) & 7
    I = lambda nombre, **k: Instr(dir_, 0, nombre, **k)  # noqa: E731

    # ---------------------------------------------------------------- 0000
    if g == 0:
        if op & 0x100:                                           # bits dinámicos o MOVEP
            if modo == 1:                                        # MOVEP
                tam = "l" if op & 0x40 else "w"
                mem = EA("d16(An)", reg, disp=_s16(rd.w()))
                dreg = EA("Dn", reg9)
                if op & 0x80:
                    return I("movep", tam=tam, ops=[dreg, mem])  # registro -> memoria
                return I("movep", tam=tam, ops=[mem, dreg])
            nombre = ["btst", "bchg", "bclr", "bset"][(op >> 6) & 3]
            ea = _ea(rd, modo, reg, "b")
            clase = _DATOS if nombre == "btst" else _DATOS_ALTERABLE
            if not _ok(ea, clase):
                return None
            return I(nombre, tam="l" if ea.modo == "Dn" else "b", ops=[EA("Dn", reg9), ea])
        alto = (op >> 9) & 7
        if alto == 4:                                            # bits estáticos (#imm)
            nombre = ["btst", "bchg", "bclr", "bset"][(op >> 6) & 3]
            num = rd.w() & 0xFF
            ea = _ea(rd, modo, reg, "b")
            clase = _DATOS - {"#imm"} if nombre == "btst" else _DATOS_ALTERABLE
            if not _ok(ea, clase):
                return None
            return I(nombre, tam="l" if ea.modo == "Dn" else "b", ops=[EA("#imm", valor=num), ea])
        nombres = {0: "ori", 1: "andi", 2: "subi", 3: "addi", 5: "eori", 6: "cmpi"}
        if alto not in nombres:
            return None
        nombre = nombres[alto]
        if nombre in ("ori", "andi", "eori") and (op & 0xFF) in (0x3C, 0x7C):   # a CCR / SR
            if (op & 0xFF) == 0x3C:
                return I(nombre + "_ccr", tam="b", ops=[EA("#imm", valor=rd.w() & 0xFF)])
            return I(nombre + "_sr", tam="w", ops=[EA("#imm", valor=rd.w())])
        tam = _tam2((op >> 6) & 3)
        if tam is None:
            return None
        imm = rd.l() if tam == "l" else (rd.w() & (0xFF if tam == "b" else 0xFFFF))
        ea = _ea(rd, modo, reg, tam)
        clase = (_DATOS - {"#imm"}) if nombre == "cmpi" else _DATOS_ALTERABLE
        if not _ok(ea, clase):
            return None
        return I(nombre, tam=tam, ops=[EA("#imm", valor=imm), ea])

    # ---------------------------------------------------------------- 0001/0010/0011: MOVE
    if g in (1, 2, 3):
        tam = {1: "b", 3: "w", 2: "l"}[g]
        src = _ea(rd, modo, reg, tam)
        if not _ok(src, _TODOS) or (tam == "b" and src.modo == "An"):
            return None
        if modo6 == 1:                                           # MOVEA
            if tam == "b":
                return None
            return I("movea", tam=tam, ops=[src, EA("An", reg9)])
        dst = _ea(rd, modo6, reg9, tam)
        if not _ok(dst, _DATOS_ALTERABLE):
            return None
        return I("move", tam=tam, ops=[src, dst])

    # ---------------------------------------------------------------- 0100: misceláneas
    if g == 4:
        if op == 0x4E70: return I("reset")
        if op == 0x4E71: return I("nop")
        if op == 0x4E72: return I("stop", ops=[EA("#imm", valor=rd.w())])
        if op == 0x4E73: return I("rte")
        if op == 0x4E75: return I("rts")
        if op == 0x4E76: return I("trapv")
        if op == 0x4E77: return I("rtr")
        if op == 0x4AFC: return I("illegal")
        if op & 0xFFF0 == 0x4E40: return I("trap", dato=op & 0xF)
        if op & 0xFFF8 == 0x4E50: return I("link", ops=[EA("An", reg)], dato=_s16(rd.w()))
        if op & 0xFFF8 == 0x4E58: return I("unlk", ops=[EA("An", reg)])
        if op & 0xFFF8 == 0x4E60: return I("move_to_usp", ops=[EA("An", reg)])
        if op & 0xFFF8 == 0x4E68: return I("move_from_usp", ops=[EA("An", reg)])
        if op & 0xFFF8 == 0x4840: return I("swap", ops=[EA("Dn", reg)])
        if op & 0xFFB8 == 0x4880:                                # EXT
            return I("ext", tam="l" if op & 0x40 else "w", ops=[EA("Dn", reg)])
        if op & 0xFFC0 in (0x4E80, 0x4EC0):                      # JSR / JMP
            ea = _ea(rd, modo, reg, None)
            if not _ok(ea, _CONTROL):
                return None
            return I("jsr" if op & 0xFFC0 == 0x4E80 else "jmp", ops=[ea])
        if op & 0xFB80 == 0x4880:                                # MOVEM
            tam = "l" if op & 0x40 else "w"
            mascara = rd.w()
            ea = _ea(rd, modo, reg, tam)
            if op & 0x400:                                       # memoria -> registros
                if not _ok(ea, _CONTROL | {"(An)+"}):
                    return None
                return I("movem_carga", tam=tam, ops=[ea], lista=mascara)
            if not _ok(ea, (_CONTROL - {"d16(PC)", "d8(PC,Xi)"}) | {"-(An)"}):
                return None
            if ea.modo == "-(An)":                               # en -(An) la máscara va invertida
                mascara = int(f"{mascara:016b}"[::-1], 2)
            return I("movem_guarda", tam=tam, ops=[ea], lista=mascara)
        if op & 0xF1C0 == 0x41C0:                                # LEA
            ea = _ea(rd, modo, reg, None)
            if not _ok(ea, _CONTROL):
                return None
            return I("lea", tam="l", ops=[ea, EA("An", reg9)])
        if op & 0xF1C0 == 0x4180:                                # CHK.W
            ea = _ea(rd, modo, reg, "w")
            if not _ok(ea, _DATOS):
                return None
            return I("chk", tam="w", ops=[ea, EA("Dn", reg9)])
        if op & 0xFFC0 == 0x4840:                                # PEA
            ea = _ea(rd, modo, reg, None)
            if not _ok(ea, _CONTROL):
                return None
            return I("pea", tam="l", ops=[ea])
        if op & 0xFFC0 == 0x4800:                                # NBCD
            ea = _ea(rd, modo, reg, "b")
            return I("nbcd", tam="b", ops=[ea]) if _ok(ea, _DATOS_ALTERABLE) else None
        if op & 0xFFC0 == 0x40C0:                                # MOVE from SR
            ea = _ea(rd, modo, reg, "w")
            return I("move_from_sr", tam="w", ops=[ea]) if _ok(ea, _DATOS_ALTERABLE) else None
        if op & 0xFFC0 == 0x44C0:                                # MOVE to CCR
            ea = _ea(rd, modo, reg, "w")
            return I("move_to_ccr", tam="w", ops=[ea]) if _ok(ea, _DATOS) else None
        if op & 0xFFC0 == 0x46C0:                                # MOVE to SR
            ea = _ea(rd, modo, reg, "w")
            return I("move_to_sr", tam="w", ops=[ea]) if _ok(ea, _DATOS) else None
        if op & 0xFFC0 == 0x4AC0:                                # TAS
            ea = _ea(rd, modo, reg, "b")
            return I("tas", tam="b", ops=[ea]) if _ok(ea, _DATOS_ALTERABLE) else None
        unarias = {0x4000: "negx", 0x4200: "clr", 0x4400: "neg", 0x4600: "not", 0x4A00: "tst"}
        if op & 0xFF00 in unarias:
            tam = _tam2((op >> 6) & 3)
            if tam is None:
                return None
            ea = _ea(rd, modo, reg, tam)
            nombre = unarias[op & 0xFF00]
            clase = (_DATOS - {"#imm", "d16(PC)", "d8(PC,Xi)"}) if nombre == "tst" else _DATOS_ALTERABLE
            return I(nombre, tam=tam, ops=[ea]) if _ok(ea, clase) else None
        return None

    # ---------------------------------------------------------------- 0101: ADDQ/SUBQ/Scc/DBcc
    if g == 5:
        if (op >> 6) & 3 == 3:
            cc = CONDICIONES[(op >> 8) & 0xF]
            if modo == 1:                                        # DBcc
                base = rd.pos
                return I("dbcc", cc=cc, ops=[EA("Dn", reg)], destino=(base + _s16(rd.w())) & 0xFFFFFF)
            ea = _ea(rd, modo, reg, "b")
            return I("scc", cc=cc, tam="b", ops=[ea]) if _ok(ea, _DATOS_ALTERABLE) else None
        tam = _tam2((op >> 6) & 3)
        dato = reg9 or 8
        ea = _ea(rd, modo, reg, tam)
        if not _ok(ea, _ALTERABLE) or (tam == "b" and ea.modo == "An"):
            return None
        return I("subq" if op & 0x100 else "addq", tam=tam, ops=[EA("#imm", valor=dato), ea])

    # ---------------------------------------------------------------- 0110: Bcc/BRA/BSR
    if g == 6:
        cond = (op >> 8) & 0xF
        d8 = op & 0xFF
        base = rd.pos
        if d8 == 0:
            disp = _s16(rd.w())
        elif d8 == 0xFF:
            return None                                          # Bcc.L no existe en el 68000
        else:
            disp = _s8(d8)
        destino = (base + disp) & 0xFFFFFF
        if cond == 0:
            return I("bra", destino=destino)
        if cond == 1:
            return I("bsr", destino=destino)
        return I("bcc", cc=CONDICIONES[cond], destino=destino)

    # ---------------------------------------------------------------- 0111: MOVEQ
    if g == 7:
        if op & 0x100:
            return None
        return I("moveq", tam="l", ops=[EA("#imm", valor=_s8(op & 0xFF) & 0xFFFFFFFF), EA("Dn", reg9)])

    # ---------------------------------------------------------------- 1000 / 1100: OR, AND y compañía
    if g in (8, 12):
        logica = "or" if g == 8 else "and"
        if modo6 == 3:                                           # DIVU / MULU .W
            ea = _ea(rd, modo, reg, "w")
            return I("divu" if g == 8 else "mulu", tam="w", ops=[ea, EA("Dn", reg9)]) if _ok(ea, _DATOS) else None
        if modo6 == 7:                                           # DIVS / MULS .W
            ea = _ea(rd, modo, reg, "w")
            return I("divs" if g == 8 else "muls", tam="w", ops=[ea, EA("Dn", reg9)]) if _ok(ea, _DATOS) else None
        if op & 0x1F0 == 0x100:                                  # SBCD / ABCD
            nombre = "sbcd" if g == 8 else "abcd"
            if op & 8:
                return I(nombre, tam="b", ops=[EA("-(An)", reg), EA("-(An)", reg9)])
            return I(nombre, tam="b", ops=[EA("Dn", reg), EA("Dn", reg9)])
        if g == 12 and op & 0x1F8 in (0x140, 0x148, 0x188):     # EXG
            tipo = op & 0x1F8
            if tipo == 0x140:
                return I("exg", tam="l", ops=[EA("Dn", reg9), EA("Dn", reg)])
            if tipo == 0x148:
                return I("exg", tam="l", ops=[EA("An", reg9), EA("An", reg)])
            return I("exg", tam="l", ops=[EA("Dn", reg9), EA("An", reg)])
        tam = _tam2((op >> 6) & 3)
        if tam is None:
            return None
        if op & 0x100:                                           # Dn op <ea> -> <ea>
            ea = _ea(rd, modo, reg, tam)
            return I(logica, tam=tam, ops=[EA("Dn", reg9), ea]) if _ok(ea, _MEM_ALTERABLE) else None
        ea = _ea(rd, modo, reg, tam)
        return I(logica, tam=tam, ops=[ea, EA("Dn", reg9)]) if _ok(ea, _DATOS) else None

    # ---------------------------------------------------------------- 1001 / 1101: SUB / ADD
    if g in (9, 13):
        nombre = "sub" if g == 9 else "add"
        if modo6 in (3, 7):                                      # SUBA / ADDA
            tam = "l" if modo6 == 7 else "w"
            ea = _ea(rd, modo, reg, tam)
            return I(nombre + "a", tam=tam, ops=[ea, EA("An", reg9)]) if _ok(ea, _TODOS) else None
        tam = _tam2((op >> 6) & 3)
        if op & 0x130 == 0x100:                                  # SUBX / ADDX
            if op & 8:
                return I(nombre + "x", tam=tam, ops=[EA("-(An)", reg), EA("-(An)", reg9)])
            return I(nombre + "x", tam=tam, ops=[EA("Dn", reg), EA("Dn", reg9)])
        if op & 0x100:
            ea = _ea(rd, modo, reg, tam)
            return I(nombre, tam=tam, ops=[EA("Dn", reg9), ea]) if _ok(ea, _MEM_ALTERABLE) else None
        ea = _ea(rd, modo, reg, tam)
        if not _ok(ea, _TODOS) or (tam == "b" and ea.modo == "An"):
            return None
        return I(nombre, tam=tam, ops=[ea, EA("Dn", reg9)])

    # ---------------------------------------------------------------- 1011: CMP/CMPA/CMPM/EOR
    if g == 11:
        if modo6 in (3, 7):
            tam = "l" if modo6 == 7 else "w"
            ea = _ea(rd, modo, reg, tam)
            return I("cmpa", tam=tam, ops=[ea, EA("An", reg9)]) if _ok(ea, _TODOS) else None
        tam = _tam2((op >> 6) & 3)
        if op & 0x100:
            if modo == 1:                                        # CMPM (Ay)+,(Ax)+
                return I("cmpm", tam=tam, ops=[EA("(An)+", reg), EA("(An)+", reg9)])
            ea = _ea(rd, modo, reg, tam)
            return I("eor", tam=tam, ops=[EA("Dn", reg9), ea]) if _ok(ea, _DATOS_ALTERABLE) else None
        ea = _ea(rd, modo, reg, tam)
        if not _ok(ea, _TODOS) or (tam == "b" and ea.modo == "An"):
            return None
        return I("cmp", tam=tam, ops=[ea, EA("Dn", reg9)])

    # ---------------------------------------------------------------- 1110: desplazamientos/rotaciones
    if g == 14:
        tipos = ["as", "ls", "rox", "ro"]
        izq = bool(op & 0x100)
        if (op >> 6) & 3 == 3:                                   # en memoria, una posición, .w
            tipo = tipos[(op >> 9) & 3]
            if op & 0x800:
                return None
            ea = _ea(rd, modo, reg, "w")
            if not _ok(ea, _MEM_ALTERABLE):
                return None
            return I(tipo + ("l" if izq else "r"), tam="w", ops=[ea], dato=1)
        tam = _tam2((op >> 6) & 3)
        tipo = tipos[(op >> 3) & 3]
        nombre = tipo + ("l" if izq else "r")
        if op & 0x20:                                            # cuenta en registro
            return I(nombre, tam=tam, ops=[EA("Dn", reg9), EA("Dn", reg)])
        return I(nombre, tam=tam, ops=[EA("#imm", valor=reg9 or 8), EA("Dn", reg)])

    # 1010 (line A) y 1111 (line F): excepciones, no instrucciones
    return None
