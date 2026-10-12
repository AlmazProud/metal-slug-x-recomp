"""Motorola 68000 decoder (68000 only) for the recompiler.

Decodes according to the encoding in the M68000 Programmer's Reference Manual. It does not depend
on Capstone, which in 68000 mode interprets some extension words with the full 68020 format. It is
validated against Capstone (lengths and mnemonics) and against Musashi (semantics) in the tests
under tests/.

API:
    decode(read16, address) -> Instr | None
where read16(addr) returns the big-endian word at that address.
"""
from __future__ import annotations

from dataclasses import dataclass, field

CONDITIONS = ["t", "f", "hi", "ls", "cc", "cs", "ne", "eq", "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le"]
SIZE_BYTES = {"b": 1, "w": 2, "l": 4}


@dataclass
class EA:
    """Decoded effective operand (addressing mode)."""
    mode: str                  # Dn An (An) (An)+ -(An) d16(An) d8(An,Xi) abs.w abs.l d16(PC) d8(PC,Xi) #imm
    reg: int = 0               # register number (0-7) for the register modes
    disp: int = 0              # signed displacement (d16 / d8)
    xi: int = 0                # index register: 0-7 = D0-D7, 8-15 = A0-A7
    xi_long: bool = False      # .l index (True) or sign-extended .w (False)
    value: int = 0             # absolute address or immediate value (unsigned, already truncated)
    pc_ext: int = 0            # address of the extension word (base of the PC modes)

    def is_memory(self) -> bool:
        return self.mode not in ("Dn", "An", "#imm")

    def text(self) -> str:
        x = ("a" if self.xi >= 8 else "d") + str(self.xi & 7) + (".l" if self.xi_long else ".w")
        return {
            "Dn": f"d{self.reg}", "An": f"a{self.reg}", "(An)": f"(a{self.reg})",
            "(An)+": f"(a{self.reg})+", "-(An)": f"-(a{self.reg})", "d16(An)": f"{self.disp}(a{self.reg})",
            "d8(An,Xi)": f"{self.disp}(a{self.reg},{x})", "abs.w": f"${self.value:x}.w",
            "abs.l": f"${self.value:x}.l", "d16(PC)": f"${self.pc_ext + self.disp:x}(pc)",
            "d8(PC,Xi)": f"${self.pc_ext + self.disp:x}(pc,{x})", "#imm": f"#${self.value:x}",
        }[self.mode]


@dataclass
class Instr:
    addr: int
    length: int                # total bytes
    op: str                    # canonical name: move, add, bcc, dbcc, ...
    size: str | None = None    # b / w / l
    ops: list = field(default_factory=list)   # EA operands (source first)
    cc: str | None = None      # condition of Bcc/Scc/DBcc
    target: int | None = None  # target of relative branches (Bcc/BSR/DBcc)
    reglist: int = 0           # MOVEM mask (bit 0 = D0 ... bit 15 = A7, not reversed)
    data: int = 0              # extra data: shift count, TRAP vector, LINK displacement
    opcode: int = 0

    def text(self) -> str:
        n = self.op + (self.cc or "") + (f".{self.size}" if self.size else "")
        parts = [e.text() for e in self.ops]
        if self.target is not None:
            parts.append(f"${self.target:x}")
        return f"{n} {', '.join(parts)}".strip()


class _Reader:
    def __init__(self, read16, addr):
        self.read16 = read16
        self.start = addr
        self.pos = addr + 2

    def w(self) -> int:
        v = self.read16(self.pos) & 0xFFFF
        self.pos += 2
        return v

    def l(self) -> int:
        high = self.w()
        return (high << 16) | self.w()


def _s8(v): return v - 0x100 if v & 0x80 else v
def _s16(v): return v - 0x10000 if v & 0x8000 else v


def _ea(rd: _Reader, mode: int, reg: int, size: str | None) -> EA | None:
    """Decodes an EA (mode, register), reading its extension words."""
    if mode == 0:
        return EA("Dn", reg)
    if mode == 1:
        return EA("An", reg)
    if mode == 2:
        return EA("(An)", reg)
    if mode == 3:
        return EA("(An)+", reg)
    if mode == 4:
        return EA("-(An)", reg)
    if mode == 5:
        return EA("d16(An)", reg, disp=_s16(rd.w()))
    if mode == 6:
        ext = rd.w()           # brief format: the 68000 ignores bits 8-10
        return EA("d8(An,Xi)", reg, disp=_s8(ext & 0xFF), xi=ext >> 12, xi_long=bool(ext & 0x800))
    if mode == 7:
        if reg == 0:
            return EA("abs.w", value=_s16(rd.w()) & 0xFFFFFFFF)
        if reg == 1:
            return EA("abs.l", value=rd.l())
        if reg == 2:
            base = rd.pos
            return EA("d16(PC)", disp=_s16(rd.w()), pc_ext=base)
        if reg == 3:
            base = rd.pos
            ext = rd.w()
            return EA("d8(PC,Xi)", disp=_s8(ext & 0xFF), xi=ext >> 12, xi_long=bool(ext & 0x800), pc_ext=base)
        if reg == 4 and size is not None:
            if size == "l":
                return EA("#imm", value=rd.l())
            v = rd.w()
            return EA("#imm", value=(v & 0xFF) if size == "b" else v)
    return None


# Valid EA classes for each operation (anything the 68000 does not accept is rejected).
_DATA_ALTERABLE = {"Dn", "(An)", "(An)+", "-(An)", "d16(An)", "d8(An,Xi)", "abs.w", "abs.l"}
_MEM_ALTERABLE = _DATA_ALTERABLE - {"Dn"}
_ALL = _DATA_ALTERABLE | {"An", "d16(PC)", "d8(PC,Xi)", "#imm"}
_DATA = _ALL - {"An"}
_CONTROL = {"(An)", "d16(An)", "d8(An,Xi)", "abs.w", "abs.l", "d16(PC)", "d8(PC,Xi)"}
_ALTERABLE = _DATA_ALTERABLE | {"An"}


def _size2(bits):  # sizes in bits 7-6: 00=b 01=w 10=l
    return {0: "b", 1: "w", 2: "l"}.get(bits)


def decode(read16, addr: int) -> Instr | None:
    op = read16(addr) & 0xFFFF
    rd = _Reader(read16, addr)
    i = _decode(op, rd, addr)
    if i is not None:
        i.length = rd.pos - addr
        i.opcode = op
    return i


def _ok(ea, ea_class):
    return ea is not None and ea.mode in ea_class


def _decode(op: int, rd: _Reader, addr: int) -> Instr | None:  # noqa: C901 (decoding table)
    g = op >> 12
    mode, reg = (op >> 3) & 7, op & 7
    reg9, mode6 = (op >> 9) & 7, (op >> 6) & 7
    I = lambda name, **k: Instr(addr, 0, name, **k)  # noqa: E731

    # ---------------------------------------------------------------- 0000
    if g == 0:
        if op & 0x100:                                           # dynamic bit ops or MOVEP
            if mode == 1:                                        # MOVEP
                size = "l" if op & 0x40 else "w"
                mem = EA("d16(An)", reg, disp=_s16(rd.w()))
                dreg = EA("Dn", reg9)
                if op & 0x80:
                    return I("movep", size=size, ops=[dreg, mem])  # register -> memory
                return I("movep", size=size, ops=[mem, dreg])
            name = ["btst", "bchg", "bclr", "bset"][(op >> 6) & 3]
            ea = _ea(rd, mode, reg, "b")
            ea_class = _DATA if name == "btst" else _DATA_ALTERABLE
            if not _ok(ea, ea_class):
                return None
            return I(name, size="l" if ea.mode == "Dn" else "b", ops=[EA("Dn", reg9), ea])
        high = (op >> 9) & 7
        if high == 4:                                            # static bit ops (#imm)
            name = ["btst", "bchg", "bclr", "bset"][(op >> 6) & 3]
            num = rd.w() & 0xFF
            ea = _ea(rd, mode, reg, "b")
            ea_class = _DATA - {"#imm"} if name == "btst" else _DATA_ALTERABLE
            if not _ok(ea, ea_class):
                return None
            return I(name, size="l" if ea.mode == "Dn" else "b", ops=[EA("#imm", value=num), ea])
        names = {0: "ori", 1: "andi", 2: "subi", 3: "addi", 5: "eori", 6: "cmpi"}
        if high not in names:
            return None
        name = names[high]
        if name in ("ori", "andi", "eori") and (op & 0xFF) in (0x3C, 0x7C):   # to CCR / SR
            if (op & 0xFF) == 0x3C:
                return I(name + "_ccr", size="b", ops=[EA("#imm", value=rd.w() & 0xFF)])
            return I(name + "_sr", size="w", ops=[EA("#imm", value=rd.w())])
        size = _size2((op >> 6) & 3)
        if size is None:
            return None
        imm = rd.l() if size == "l" else (rd.w() & (0xFF if size == "b" else 0xFFFF))
        ea = _ea(rd, mode, reg, size)
        ea_class = (_DATA - {"#imm"}) if name == "cmpi" else _DATA_ALTERABLE
        if not _ok(ea, ea_class):
            return None
        return I(name, size=size, ops=[EA("#imm", value=imm), ea])

    # ---------------------------------------------------------------- 0001/0010/0011: MOVE
    if g in (1, 2, 3):
        size = {1: "b", 3: "w", 2: "l"}[g]
        src = _ea(rd, mode, reg, size)
        if not _ok(src, _ALL) or (size == "b" and src.mode == "An"):
            return None
        if mode6 == 1:                                           # MOVEA
            if size == "b":
                return None
            return I("movea", size=size, ops=[src, EA("An", reg9)])
        dst = _ea(rd, mode6, reg9, size)
        if not _ok(dst, _DATA_ALTERABLE):
            return None
        return I("move", size=size, ops=[src, dst])

    # ---------------------------------------------------------------- 0100: miscellaneous
    if g == 4:
        if op == 0x4E70: return I("reset")
        if op == 0x4E71: return I("nop")
        if op == 0x4E72: return I("stop", ops=[EA("#imm", value=rd.w())])
        if op == 0x4E73: return I("rte")
        if op == 0x4E75: return I("rts")
        if op == 0x4E76: return I("trapv")
        if op == 0x4E77: return I("rtr")
        if op == 0x4AFC: return I("illegal")
        if op & 0xFFF0 == 0x4E40: return I("trap", data=op & 0xF)
        if op & 0xFFF8 == 0x4E50: return I("link", ops=[EA("An", reg)], data=_s16(rd.w()))
        if op & 0xFFF8 == 0x4E58: return I("unlk", ops=[EA("An", reg)])
        if op & 0xFFF8 == 0x4E60: return I("move_to_usp", ops=[EA("An", reg)])
        if op & 0xFFF8 == 0x4E68: return I("move_from_usp", ops=[EA("An", reg)])
        if op & 0xFFF8 == 0x4840: return I("swap", ops=[EA("Dn", reg)])
        if op & 0xFFB8 == 0x4880:                                # EXT
            return I("ext", size="l" if op & 0x40 else "w", ops=[EA("Dn", reg)])
        if op & 0xFFC0 in (0x4E80, 0x4EC0):                      # JSR / JMP
            ea = _ea(rd, mode, reg, None)
            if not _ok(ea, _CONTROL):
                return None
            return I("jsr" if op & 0xFFC0 == 0x4E80 else "jmp", ops=[ea])
        if op & 0xFB80 == 0x4880:                                # MOVEM
            size = "l" if op & 0x40 else "w"
            mask = rd.w()
            ea = _ea(rd, mode, reg, size)
            if op & 0x400:                                       # memory -> registers
                if not _ok(ea, _CONTROL | {"(An)+"}):
                    return None
                return I("movem_load", size=size, ops=[ea], reglist=mask)
            if not _ok(ea, (_CONTROL - {"d16(PC)", "d8(PC,Xi)"}) | {"-(An)"}):
                return None
            if ea.mode == "-(An)":                               # with -(An) the mask is reversed
                mask = int(f"{mask:016b}"[::-1], 2)
            return I("movem_store", size=size, ops=[ea], reglist=mask)
        if op & 0xF1C0 == 0x41C0:                                # LEA
            ea = _ea(rd, mode, reg, None)
            if not _ok(ea, _CONTROL):
                return None
            return I("lea", size="l", ops=[ea, EA("An", reg9)])
        if op & 0xF1C0 == 0x4180:                                # CHK.W
            ea = _ea(rd, mode, reg, "w")
            if not _ok(ea, _DATA):
                return None
            return I("chk", size="w", ops=[ea, EA("Dn", reg9)])
        if op & 0xFFC0 == 0x4840:                                # PEA
            ea = _ea(rd, mode, reg, None)
            if not _ok(ea, _CONTROL):
                return None
            return I("pea", size="l", ops=[ea])
        if op & 0xFFC0 == 0x4800:                                # NBCD
            ea = _ea(rd, mode, reg, "b")
            return I("nbcd", size="b", ops=[ea]) if _ok(ea, _DATA_ALTERABLE) else None
        if op & 0xFFC0 == 0x40C0:                                # MOVE from SR
            ea = _ea(rd, mode, reg, "w")
            return I("move_from_sr", size="w", ops=[ea]) if _ok(ea, _DATA_ALTERABLE) else None
        if op & 0xFFC0 == 0x44C0:                                # MOVE to CCR
            ea = _ea(rd, mode, reg, "w")
            return I("move_to_ccr", size="w", ops=[ea]) if _ok(ea, _DATA) else None
        if op & 0xFFC0 == 0x46C0:                                # MOVE to SR
            ea = _ea(rd, mode, reg, "w")
            return I("move_to_sr", size="w", ops=[ea]) if _ok(ea, _DATA) else None
        if op & 0xFFC0 == 0x4AC0:                                # TAS
            ea = _ea(rd, mode, reg, "b")
            return I("tas", size="b", ops=[ea]) if _ok(ea, _DATA_ALTERABLE) else None
        unary = {0x4000: "negx", 0x4200: "clr", 0x4400: "neg", 0x4600: "not", 0x4A00: "tst"}
        if op & 0xFF00 in unary:
            size = _size2((op >> 6) & 3)
            if size is None:
                return None
            ea = _ea(rd, mode, reg, size)
            name = unary[op & 0xFF00]
            ea_class = (_DATA - {"#imm", "d16(PC)", "d8(PC,Xi)"}) if name == "tst" else _DATA_ALTERABLE
            return I(name, size=size, ops=[ea]) if _ok(ea, ea_class) else None
        return None

    # ---------------------------------------------------------------- 0101: ADDQ/SUBQ/Scc/DBcc
    if g == 5:
        if (op >> 6) & 3 == 3:
            cc = CONDITIONS[(op >> 8) & 0xF]
            if mode == 1:                                        # DBcc
                base = rd.pos
                return I("dbcc", cc=cc, ops=[EA("Dn", reg)], target=(base + _s16(rd.w())) & 0xFFFFFF)
            ea = _ea(rd, mode, reg, "b")
            return I("scc", cc=cc, size="b", ops=[ea]) if _ok(ea, _DATA_ALTERABLE) else None
        size = _size2((op >> 6) & 3)
        quick = reg9 or 8
        ea = _ea(rd, mode, reg, size)
        if not _ok(ea, _ALTERABLE) or (size == "b" and ea.mode == "An"):
            return None
        return I("subq" if op & 0x100 else "addq", size=size, ops=[EA("#imm", value=quick), ea])

    # ---------------------------------------------------------------- 0110: Bcc/BRA/BSR
    if g == 6:
        cond = (op >> 8) & 0xF
        d8 = op & 0xFF
        base = rd.pos
        if d8 == 0:
            disp = _s16(rd.w())
        elif d8 == 0xFF:
            return None                                          # Bcc.L does not exist on the 68000
        else:
            disp = _s8(d8)
        target = (base + disp) & 0xFFFFFF
        if cond == 0:
            return I("bra", target=target)
        if cond == 1:
            return I("bsr", target=target)
        return I("bcc", cc=CONDITIONS[cond], target=target)

    # ---------------------------------------------------------------- 0111: MOVEQ
    if g == 7:
        if op & 0x100:
            return None
        return I("moveq", size="l", ops=[EA("#imm", value=_s8(op & 0xFF) & 0xFFFFFFFF), EA("Dn", reg9)])

    # ---------------------------------------------------------------- 1000 / 1100: OR, AND and friends
    if g in (8, 12):
        logic_op = "or" if g == 8 else "and"
        if mode6 == 3:                                           # DIVU / MULU .W
            ea = _ea(rd, mode, reg, "w")
            return I("divu" if g == 8 else "mulu", size="w", ops=[ea, EA("Dn", reg9)]) if _ok(ea, _DATA) else None
        if mode6 == 7:                                           # DIVS / MULS .W
            ea = _ea(rd, mode, reg, "w")
            return I("divs" if g == 8 else "muls", size="w", ops=[ea, EA("Dn", reg9)]) if _ok(ea, _DATA) else None
        if op & 0x1F0 == 0x100:                                  # SBCD / ABCD
            name = "sbcd" if g == 8 else "abcd"
            if op & 8:
                return I(name, size="b", ops=[EA("-(An)", reg), EA("-(An)", reg9)])
            return I(name, size="b", ops=[EA("Dn", reg), EA("Dn", reg9)])
        if g == 12 and op & 0x1F8 in (0x140, 0x148, 0x188):     # EXG
            kind = op & 0x1F8
            if kind == 0x140:
                return I("exg", size="l", ops=[EA("Dn", reg9), EA("Dn", reg)])
            if kind == 0x148:
                return I("exg", size="l", ops=[EA("An", reg9), EA("An", reg)])
            return I("exg", size="l", ops=[EA("Dn", reg9), EA("An", reg)])
        size = _size2((op >> 6) & 3)
        if size is None:
            return None
        if op & 0x100:                                           # Dn op <ea> -> <ea>
            ea = _ea(rd, mode, reg, size)
            return I(logic_op, size=size, ops=[EA("Dn", reg9), ea]) if _ok(ea, _MEM_ALTERABLE) else None
        ea = _ea(rd, mode, reg, size)
        return I(logic_op, size=size, ops=[ea, EA("Dn", reg9)]) if _ok(ea, _DATA) else None

    # ---------------------------------------------------------------- 1001 / 1101: SUB / ADD
    if g in (9, 13):
        name = "sub" if g == 9 else "add"
        if mode6 in (3, 7):                                      # SUBA / ADDA
            size = "l" if mode6 == 7 else "w"
            ea = _ea(rd, mode, reg, size)
            return I(name + "a", size=size, ops=[ea, EA("An", reg9)]) if _ok(ea, _ALL) else None
        size = _size2((op >> 6) & 3)
        if op & 0x130 == 0x100:                                  # SUBX / ADDX
            if op & 8:
                return I(name + "x", size=size, ops=[EA("-(An)", reg), EA("-(An)", reg9)])
            return I(name + "x", size=size, ops=[EA("Dn", reg), EA("Dn", reg9)])
        if op & 0x100:
            ea = _ea(rd, mode, reg, size)
            return I(name, size=size, ops=[EA("Dn", reg9), ea]) if _ok(ea, _MEM_ALTERABLE) else None
        ea = _ea(rd, mode, reg, size)
        if not _ok(ea, _ALL) or (size == "b" and ea.mode == "An"):
            return None
        return I(name, size=size, ops=[ea, EA("Dn", reg9)])

    # ---------------------------------------------------------------- 1011: CMP/CMPA/CMPM/EOR
    if g == 11:
        if mode6 in (3, 7):
            size = "l" if mode6 == 7 else "w"
            ea = _ea(rd, mode, reg, size)
            return I("cmpa", size=size, ops=[ea, EA("An", reg9)]) if _ok(ea, _ALL) else None
        size = _size2((op >> 6) & 3)
        if op & 0x100:
            if mode == 1:                                        # CMPM (Ay)+,(Ax)+
                return I("cmpm", size=size, ops=[EA("(An)+", reg), EA("(An)+", reg9)])
            ea = _ea(rd, mode, reg, size)
            return I("eor", size=size, ops=[EA("Dn", reg9), ea]) if _ok(ea, _DATA_ALTERABLE) else None
        ea = _ea(rd, mode, reg, size)
        if not _ok(ea, _ALL) or (size == "b" and ea.mode == "An"):
            return None
        return I("cmp", size=size, ops=[ea, EA("Dn", reg9)])

    # ---------------------------------------------------------------- 1110: shifts/rotates
    if g == 14:
        kinds = ["as", "ls", "rox", "ro"]
        left = bool(op & 0x100)
        if (op >> 6) & 3 == 3:                                   # in memory, one position, .w
            kind = kinds[(op >> 9) & 3]
            if op & 0x800:
                return None
            ea = _ea(rd, mode, reg, "w")
            if not _ok(ea, _MEM_ALTERABLE):
                return None
            return I(kind + ("l" if left else "r"), size="w", ops=[ea], data=1)
        size = _size2((op >> 6) & 3)
        kind = kinds[(op >> 3) & 3]
        name = kind + ("l" if left else "r")
        if op & 0x20:                                            # count in a register
            return I(name, size=size, ops=[EA("Dn", reg9), EA("Dn", reg)])
        return I(name, size=size, ops=[EA("#imm", value=reg9 or 8), EA("Dn", reg)])

    # 1010 (line A) and 1111 (line F): exceptions, not instructions
    return None
