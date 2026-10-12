"""C generator for decoded 68000 instructions (decoder.py).

Translates an instruction into C statements that operate on `Cpu68k *c` (src/cpu/cpu68k.h).
Control flow is expressed through macros defined by whoever includes the generated code:

    PD_JUMP(target)        jump to a known address
    PD_JUMP_DYN(expr)      jump to an address computed at run time
    PD_NEXT(addr)          continue with the next instruction (addr = its address)
    PD_EXCEPTION(vector, return_pc)

This way the same generator serves the differential tests (one function per instruction)
and the port (whole functions with labels).

The generated C code is derived from the game program: it is generated at build time from
the user's ROMs and is never committed.
"""
from __future__ import annotations

from decoder import EA, Instr, SIZE_BYTES

MASK = {"b": "MASK_B", "w": "MASK_W", "l": "MASK_L"}
MSB = {"b": "MSB_B", "w": "MSB_W", "l": "MSB_L"}
CC_C = {"t": "CC_T", "f": "CC_F", "hi": "CC_HI", "ls": "CC_LS", "cc": "CC_CC", "cs": "CC_CS",
        "ne": "CC_NE", "eq": "CC_EQ", "vc": "CC_VC", "vs": "CC_VS", "pl": "CC_PL", "mi": "CC_MI",
        "ge": "CC_GE", "lt": "CC_LT", "gt": "CC_GT", "le": "CC_LE"}


# 68000 exception cycles (Musashi 4.60 table for the 68000, MIT).
EXCEPTION_CYCLES = {4: 34, 5: 38, 6: 40, 7: 34, 8: 34, **{32 + n: 34 for n in range(16)}}

# Base cycles per opcode (tools/recomp68k/dump_cycles.c -> 65536-byte file).
BASE_CYCLES: bytes | None = None


def load_cycles(path) -> None:
    global BASE_CYCLES
    with open(path, "rb") as f:
        BASE_CYCLES = f.read()
    assert len(BASE_CYCLES) == 0x10000


class Unsupported(Exception):
    pass


def _rd(size: str) -> str:
    return {"b": "bus_r8", "w": "bus_r16", "l": "bus_r32"}[size]


def _wr(size: str) -> str:
    return {"b": "bus_w8", "w": "bus_w16", "l": "bus_w32"}[size]


class Emitter:
    """Emits C for one instruction. `lines` accumulates the statements."""

    def __init__(self, ins: Instr):
        self.i = ins
        self.lines: list[str] = []
        self.n = 0

    # ------------------------------------------------------------ helpers
    def tmp(self, prefix="t") -> str:
        self.n += 1
        return f"{prefix}{self.n}"

    def e(self, s: str) -> None:
        self.lines.append(s)

    def next_addr(self) -> int:
        return (self.i.addr + self.i.length) & 0xFFFFFF

    def index(self, ea: EA) -> str:
        r = f"c->a[{ea.xi & 7}]" if ea.xi >= 8 else f"c->d[{ea.xi & 7}]"
        return r if ea.xi_long else f"sext16({r} & 0xFFFF)"

    def step(self, ea: EA, size: str) -> int:
        n = SIZE_BYTES[size]
        return 2 if (n == 1 and ea.reg == 7) else n

    def address(self, ea: EA, size: str | None) -> str:
        """Computes the effective address (with (An)+ / -(An) side effects) and returns a variable."""
        v = self.tmp("ea")
        m = ea.mode
        if m == "(An)":
            self.e(f"uint32_t {v} = c->a[{ea.reg}];")
        elif m == "(An)+":
            self.e(f"uint32_t {v} = c->a[{ea.reg}]; c->a[{ea.reg}] += {self.step(ea, size)};")
        elif m == "-(An)":
            self.e(f"c->a[{ea.reg}] -= {self.step(ea, size)}; uint32_t {v} = c->a[{ea.reg}];")
        elif m == "d16(An)":
            self.e(f"uint32_t {v} = c->a[{ea.reg}] + (uint32_t)({ea.disp});")
        elif m == "d8(An,Xi)":
            self.e(f"uint32_t {v} = c->a[{ea.reg}] + (uint32_t)({ea.disp}) + {self.index(ea)};")
        elif m in ("abs.w", "abs.l"):
            self.e(f"uint32_t {v} = 0x{ea.value & 0xFFFFFFFF:X}u;")
        elif m == "d16(PC)":
            self.e(f"uint32_t {v} = 0x{(ea.pc_ext + ea.disp) & 0xFFFFFFFF:X}u;")
        elif m == "d8(PC,Xi)":
            self.e(f"uint32_t {v} = 0x{(ea.pc_ext + ea.disp) & 0xFFFFFFFF:X}u + {self.index(ea)};")
        else:
            raise Unsupported(f"address of {m}")
        return v

    def read(self, ea: EA, size: str, addr_var: str | None = None) -> str:
        """Reads an operand; for memory, `addr_var` lets an already computed address be reused."""
        v = self.tmp("v")
        m = ea.mode
        if m == "Dn":
            self.e(f"uint32_t {v} = c->d[{ea.reg}] & {MASK[size]};")
        elif m == "An":
            self.e(f"uint32_t {v} = c->a[{ea.reg}] & {MASK[size]};")
        elif m == "#imm":
            self.e(f"uint32_t {v} = 0x{ea.value & 0xFFFFFFFF:X}u;")
        else:
            d = addr_var or self.address(ea, size)
            self.e(f"uint32_t {v} = {_rd(size)}({d});")
        return v

    def write(self, ea: EA, size: str, value: str, addr_var: str | None = None) -> None:
        m = ea.mode
        if m == "Dn":
            self.e(f"set_d(c, {ea.reg}, {value}, {MASK[size]});")
        elif m == "An":
            self.e(f"c->a[{ea.reg}] = {value};")
        else:
            d = addr_var or self.address(ea, size)
            self.e(f"{_wr(size)}({d}, {value});")

    def read_modify(self, ea: EA, size: str):
        """For read-modify-write operations: computes the address only once."""
        if ea.is_memory():
            d = self.address(ea, size)
            return self.read(ea, size, d), d
        return self.read(ea, size), None

    def privileged(self) -> None:
        self.e(f"if (!c->s) {{ {self.exception(8, self.i.addr)} }}")

    # ------------------------------------------------------------ emission
    def emit(self) -> list[str]:
        i = self.i
        f = getattr(self, "op_" + i.op, None)
        if f is None:
            raise Unsupported(i.op)
        if BASE_CYCLES is not None:
            self.e(f"PD_BEGIN(0x{self.i.opcode:04X}u); c->cycles += CIC({self.base()}{self.adjustment()});")
        f()
        return self.lines

    def adjustment(self) -> str:
        """Call to cycles_adjust (src/cpu/cycles68k.h) for the families whose cycles are corrected
        against MAME; empty for the rest (no need to call it)."""
        op = self.i.opcode
        mode = (op >> 3) & 7
        family = ((((op & 0xF1C0) in (0xD1C0, 0x91C0, 0xD080, 0x9080, 0xC080, 0x8080)) and mode <= 1)
                  or (op & 0xF1FF) in (0xD0FC, 0x90FC)
                  or (op & 0xF1F8) == 0x5048
                  or (op & 0xFFF8) == 0x0280
                  or (op & 0xFFF8) in (0x0840, 0x0880, 0x08C0)
                  or (op & 0xF1F8) in (0x0140, 0x0180, 0x01C0))
        if not family:
            return ""
        ext = 0
        if (op & 0xFFF8) in (0x0840, 0x0880, 0x08C0) and self.i.ops:
            ext = self.i.ops[0].value & 0xFFFF
        return f" + cycles_adjust(c, 0x{op:04X}u, 0x{ext:X}u)"

    def base(self) -> int:
        return BASE_CYCLES[self.i.opcode] if BASE_CYCLES is not None else 0

    def cycles(self, n) -> str:
        """Statement that adds `n` cycles (C text or integer); empty if there is no cycle table."""
        return f"c->cycles += CIC({n}); " if BASE_CYCLES is not None else ""

    def exception(self, vector: int, pc: int) -> str:
        """Exception statement: its cost replaces the instruction's."""
        return f"{self.cycles(EXCEPTION_CYCLES[vector] - self.base())}PD_EXCEPTION({vector}, 0x{pc:X}u);"

    def finish(self) -> None:
        self.e(f"PD_NEXT(0x{self.next_addr():X}u);")

    # --- moves
    def op_move(self):
        t = self.i.size
        s = self.read(self.i.ops[0], t)
        r = self.tmp()
        self.e(f"uint32_t {r} = op_logic(c, {s}, {MASK[t]}, {MSB[t]});")
        self.write(self.i.ops[1], t, r)
        self.finish()

    def op_moveq(self):
        self.e(f"c->d[{self.i.ops[1].reg}] = op_logic(c, 0x{self.i.ops[0].value:X}u, MASK_L, MSB_L);")
        self.finish()

    def op_movea(self):
        t = self.i.size
        s = self.read(self.i.ops[0], t)
        self.e(f"c->a[{self.i.ops[1].reg}] = {'sext16(' + s + ')' if t == 'w' else s};")
        self.finish()

    def op_lea(self):
        d = self.address(self.i.ops[0], None)
        self.e(f"c->a[{self.i.ops[1].reg}] = {d};")
        self.finish()

    def op_pea(self):
        d = self.address(self.i.ops[0], None)
        self.e(f"c->a[7] -= 4; bus_w32(c->a[7], {d});")
        self.finish()

    def op_clr(self):
        t = self.i.size
        ea = self.i.ops[0]
        if ea.is_memory():
            d = self.address(ea, t)
            self.e(f"(void){_rd(t)}({d});")      # the 68000 reads before writing in CLR
            self.write(ea, t, "0", d)
        else:
            self.write(ea, t, "0")
        self.e("c->n = 0; c->z = 1; c->v = 0; c->c = 0;")
        self.finish()

    def op_tst(self):
        t = self.i.size
        s = self.read(self.i.ops[0], t)
        self.e(f"(void)op_logic(c, {s}, {MASK[t]}, {MSB[t]});")
        self.finish()

    def op_exg(self):
        a, b = self.i.ops
        ra = f"c->{'d' if a.mode == 'Dn' else 'a'}[{a.reg}]"
        rb = f"c->{'d' if b.mode == 'Dn' else 'a'}[{b.reg}]"
        t = self.tmp()
        self.e(f"uint32_t {t} = {ra}; {ra} = {rb}; {rb} = {t};")
        self.finish()

    def op_swap(self):
        r = self.i.ops[0].reg
        self.e(f"c->d[{r}] = op_logic(c, c->d[{r}] >> 16 | c->d[{r}] << 16, MASK_L, MSB_L);")
        self.finish()

    def op_ext(self):
        r = self.i.ops[0].reg
        if self.i.size == "w":
            self.e(f"set_d(c, {r}, op_logic(c, sext8(c->d[{r}] & 0xFF), MASK_W, MSB_W), MASK_W);")
        else:
            self.e(f"c->d[{r}] = op_logic(c, sext16(c->d[{r}] & 0xFFFF), MASK_L, MSB_L);")
        self.finish()

    # --- binary arithmetic and logic
    def _binary(self, func: str, writes=True):
        t = self.i.size
        src, dst = self.i.ops
        s = self.read(src, t)
        d, addr = self.read_modify(dst, t)
        r = self.tmp()
        if func == "cmp":
            self.e(f"op_cmp(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
        elif func in ("and", "or", "eor"):
            opc = {"and": "&", "or": "|", "eor": "^"}[func]
            self.e(f"uint32_t {r} = op_logic(c, {d} {opc} {s}, {MASK[t]}, {MSB[t]});")
        else:
            self.e(f"uint32_t {r} = op_{func}(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
        if writes and func != "cmp":
            self.write(dst, t, r, addr)
        self.finish()

    def op_add(self):  self._binary("add")
    def op_addi(self): self._binary("add")
    def op_sub(self):  self._binary("sub")
    def op_subi(self): self._binary("sub")
    def op_cmp(self):  self._binary("cmp")
    def op_cmpi(self): self._binary("cmp")
    def op_and(self):  self._binary("and")
    def op_andi(self): self._binary("and")
    def op_or(self):   self._binary("or")
    def op_ori(self):  self._binary("or")
    def op_eor(self):  self._binary("eor")
    def op_eori(self): self._binary("eor")

    def _quick(self, func: str):
        src, dst = self.i.ops
        if dst.mode == "An":                         # ADDQ/SUBQ to An: 32 bits, no flags
            sign = "+" if func == "add" else "-"
            self.e(f"c->a[{dst.reg}] {sign}= {src.value};")
            self.finish()
            return
        self._binary(func)

    def op_addq(self): self._quick("add")
    def op_subq(self): self._quick("sub")

    def _arith_a(self, sign: str):
        t = self.i.size
        s = self.read(self.i.ops[0], t)
        v = f"sext16({s})" if t == "w" else s
        self.e(f"c->a[{self.i.ops[1].reg}] {sign}= {v};")
        self.finish()

    def op_adda(self): self._arith_a("+")
    def op_suba(self): self._arith_a("-")

    def op_cmpa(self):
        t = self.i.size
        s = self.read(self.i.ops[0], t)
        v = f"sext16({s})" if t == "w" else s
        self.e(f"op_cmp(c, {v}, c->a[{self.i.ops[1].reg}], MASK_L, MSB_L);")
        self.finish()

    def op_cmpm(self):
        t = self.i.size
        s = self.read(self.i.ops[0], t)
        d = self.read(self.i.ops[1], t)
        self.e(f"op_cmp(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
        self.finish()

    def _extended(self, func: str):
        t = self.i.size
        src, dst = self.i.ops
        if src.mode == "Dn":
            s = self.read(src, t)
            d = self.read(dst, t)
            r = self.tmp()
            self.e(f"uint32_t {r} = op_{func}(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
            self.write(dst, t, r)
        else:                                         # -(Ay),-(Ax)
            s = self.read(EA("-(An)", src.reg), t)
            dd = self.address(EA("-(An)", dst.reg), t)
            d = self.read(dst, t, dd)
            r = self.tmp()
            self.e(f"uint32_t {r} = op_{func}(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
            self.write(dst, t, r, dd)
        self.finish()

    def op_addx(self): self._extended("addx")
    def op_subx(self): self._extended("subx")

    def _unary(self, expr: str):
        t = self.i.size
        d, addr = self.read_modify(self.i.ops[0], t)
        r = self.tmp()
        self.e(f"uint32_t {r} = {expr.format(d=d, M=MASK[t], S=MSB[t])};")
        self.write(self.i.ops[0], t, r, addr)
        self.finish()

    def op_neg(self):  self._unary("op_neg(c, {d}, {M}, {S})")
    def op_negx(self): self._unary("op_negx(c, {d}, {M}, {S})")
    def op_not(self):  self._unary("op_logic(c, ~{d}, {M}, {S})")

    # --- control flow
    def op_bra(self):
        self.e(f"PD_JUMP(0x{self.i.target:X}u);")

    def op_bcc(self):
        self.e(f"if (cond(c, {CC_C[self.i.cc]})) {{ PD_JUMP(0x{self.i.target:X}u); }}")
        self.e(self.cycles(2 if self.i.length == 4 else -2))
        self.finish()

    def op_bsr(self):
        self.e(f"c->a[7] -= 4; bus_w32(c->a[7], 0x{self.next_addr():X}u);")
        self.e(f"PD_CALL(0x{self.i.target:X}u, 0x{self.next_addr():X}u);")

    def op_jsr(self):
        d = self.address(self.i.ops[0], None)
        self.e(f"c->a[7] -= 4; bus_w32(c->a[7], 0x{self.next_addr():X}u);")
        self.e(f"PD_CALL_DYN({d}, 0x{self.next_addr():X}u);")

    def op_jmp(self):
        d = self.address(self.i.ops[0], None)
        self.e(f"PD_JUMP_DYN({d});")

    def op_rts(self):
        t = self.tmp()
        self.e(f"uint32_t {t} = bus_r32(c->a[7]); c->a[7] += 4;")
        self.e(f"PD_RETURN({t});")

    def op_rtr(self):
        t = self.tmp()
        self.e(f"cpu_set_ccr(c, bus_r16(c->a[7])); c->a[7] += 2;")
        self.e(f"uint32_t {t} = bus_r32(c->a[7]); c->a[7] += 4;")
        self.e(f"PD_RETURN({t});")

    def op_rte(self):
        self.privileged()
        sr, pc = self.tmp(), self.tmp()
        self.e(f"uint32_t {sr} = bus_r16(c->a[7]); uint32_t {pc} = bus_r32(c->a[7] + 2); c->a[7] += 6;")
        self.e(f"cpu_set_sr(c, {sr});")
        self.e(f"PD_JUMP_DYN({pc});")

    def op_dbcc(self):
        r = self.i.ops[0].reg
        self.e(f"if (!cond(c, {CC_C[self.i.cc]})) {{")
        self.e(f"    uint32_t cnt = (c->d[{r}] - 1) & 0xFFFF; set_d(c, {r}, cnt, MASK_W);")
        self.e(f"    if (cnt != 0xFFFF) {{ {self.cycles(-2)}PD_JUMP(0x{self.i.target:X}u); }}")
        self.e(f"    {self.cycles(2)}")
        self.e("}")
        self.finish()

    def op_scc(self):
        ea = self.i.ops[0]
        v = f"(cond(c, {CC_C[self.i.cc]}) ? 0xFFu : 0u)"
        if ea.is_memory():
            d = self.address(ea, "b")
            self.e(f"(void)bus_r8({d});")             # Scc reads before writing
            self.write(ea, "b", v, d)
        else:
            self.write(ea, "b", v)
            if self.i.cc != "t":                  # ST Dn already has its full cost in the base table
                self.e(f"if (cond(c, {CC_C[self.i.cc]})) {{ {self.cycles(2)}}}")
        self.finish()

    def op_nop(self):
        self.finish()

    def op_trap(self):
        self.e(self.exception(32 + self.i.data, self.next_addr()))

    def op_trapv(self):
        self.e(f"if (c->v) {{ {self.exception(7, self.next_addr())} }}")
        self.finish()

    def op_illegal(self):
        self.e(self.exception(4, self.i.addr))

    def op_link(self):
        r = self.i.ops[0].reg
        self.e(f"c->a[7] -= 4; bus_w32(c->a[7], c->a[{r}]); c->a[{r}] = c->a[7]; c->a[7] += (uint32_t)({self.i.data});")
        self.finish()

    def op_unlk(self):
        r = self.i.ops[0].reg
        self.e(f"c->a[7] = c->a[{r}]; c->a[{r}] = bus_r32(c->a[7]); c->a[7] += 4;")
        self.finish()


    # --- bits
    def _bit(self, action: str):
        src, dst = self.i.ops
        t = "l" if dst.mode == "Dn" else "b"
        modulo = 31 if t == "l" else 7
        if src.mode == "#imm":
            nbit = f"{src.value & modulo}u"
        else:
            nbit = f"(c->d[{src.reg}] & {modulo}u)"
        d, addr = self.read_modify(dst, t)
        m = self.tmp("m")
        self.e(f"uint32_t {m} = 1u << {nbit}; c->z = ({d} & {m}) == 0;")
        if action != "btst":
            expr = {"bchg": f"{d} ^ {m}", "bclr": f"{d} & ~{m}", "bset": f"{d} | {m}"}[action]
            self.write(dst, t, expr, addr)
        self.finish()

    def op_btst(self): self._bit("btst")
    def op_bchg(self): self._bit("bchg")
    def op_bclr(self): self._bit("bclr")
    def op_bset(self): self._bit("bset")

    # --- shifts and rotates
    def _shift(self, kind: str, left: bool):
        t = self.i.size
        if len(self.i.ops) == 1:                      # in memory: one position, .w
            d, addr = self.read_modify(self.i.ops[0], "w")
            r = self.tmp()
            self.e(f"uint32_t {r} = op_shift(c, SH_{kind.upper()}, {int(left)}, 1, {d}, MASK_W, MSB_W);")
            self.write(self.i.ops[0], "w", r, addr)
        else:
            cnt_ea, dst = self.i.ops
            cnt = f"{cnt_ea.value}u" if cnt_ea.mode == "#imm" else f"(c->d[{cnt_ea.reg}] & 63u)"
            self.e(self.cycles(f"2 * {cnt}"))
            r = self.tmp()
            self.e(f"uint32_t {r} = op_shift(c, SH_{kind.upper()}, {int(left)}, {cnt}, "
                   f"c->d[{dst.reg}] & {MASK[t]}, {MASK[t]}, {MSB[t]});")
            self.write(dst, t, r)
        self.finish()

    def op_asl(self):  self._shift("as", True)
    def op_asr(self):  self._shift("as", False)
    def op_lsl(self):  self._shift("ls", True)
    def op_lsr(self):  self._shift("ls", False)
    def op_roxl(self): self._shift("rox", True)
    def op_roxr(self): self._shift("rox", False)
    def op_rol(self):  self._shift("ro", True)
    def op_ror(self):  self._shift("ro", False)

    # --- multiplication and division
    def _mul(self, signed: bool):
        s = self.read(self.i.ops[0], "w")
        r = self.i.ops[1].reg
        if BASE_CYCLES is not None:
            self.e(f"c->cycles += CIC({'cycles_muls' if signed else 'cycles_mulu'}({s}));")
        if signed:
            expr = f"(uint32_t)((int32_t)(int16_t){s} * (int32_t)(int16_t)(c->d[{r}] & 0xFFFF))"
        else:
            expr = f"{s} * (c->d[{r}] & 0xFFFF)"
        self.e(f"c->d[{r}] = op_logic(c, {expr}, MASK_L, MSB_L);")
        self.finish()

    def op_mulu(self): self._mul(False)
    def op_muls(self): self._mul(True)

    def _div(self, signed: bool):
        s = self.read(self.i.ops[0], "w")
        r = self.i.ops[1].reg
        self.e(f"if ({s} == 0) {{ {self.exception(5, self.next_addr())} }}")
        if BASE_CYCLES is not None:
            self.e(f"c->cycles += CIC(cycles_adjust_div{'s' if signed else 'u'}(c->d[{r}], {s}));")
        self.e(f"op_div{'s' if signed else 'u'}(c, {r}, {s});")
        self.finish()

    def op_divu(self): self._div(False)
    def op_divs(self): self._div(True)

    # --- MOVEM / MOVEP
    def _regs(self, mask: int) -> list[str]:
        return [f"c->{'d' if n < 8 else 'a'}[{n & 7}]" for n in range(16) if mask >> n & 1]

    def op_movem_load(self):
        t = self.i.size
        ea = self.i.ops[0]
        n = SIZE_BYTES[t]
        self.e(self.cycles(bin(self.i.reglist).count("1") * (8 if t == "l" else 4)))
        if ea.mode == "(An)+":
            p = self.tmp("p")
            self.e(f"uint32_t {p} = c->a[{ea.reg}];")
        else:
            p = self.address(ea, t)
        for reg in self._regs(self.i.reglist):
            v = f"sext16(bus_r16({p}))" if t == "w" else f"bus_r32({p})"
            self.e(f"{reg} = {v}; {p} += {n};")
        if ea.mode == "(An)+":
            self.e(f"c->a[{ea.reg}] = {p};")
        self.finish()

    def op_movem_store(self):
        t = self.i.size
        ea = self.i.ops[0]
        n = SIZE_BYTES[t]
        self.e(self.cycles(bin(self.i.reglist).count("1") * (8 if t == "l" else 4)))
        regs = self._regs(self.i.reglist)
        snap = self.tmp("r")
        self.e(f"uint32_t {snap}[16]; memcpy({snap}, c->d, 32); memcpy({snap} + 8, c->a, 32);")
        idx = [k for k in range(16) if self.i.reglist >> k & 1]
        if ea.mode == "-(An)":
            p = self.tmp("p")
            self.e(f"uint32_t {p} = c->a[{ea.reg}];")
            for k in reversed(idx):
                self.e(f"{p} -= {n}; {_wr(t)}({p}, {snap}[{k}]);")
            self.e(f"c->a[{ea.reg}] = {p};")
        else:
            p = self.address(ea, t)
            for k in idx:
                self.e(f"{_wr(t)}({p}, {snap}[{k}]); {p} += {n};")
        del regs
        self.finish()

    def op_movep(self):
        t = self.i.size
        a, b = self.i.ops
        if a.mode == "Dn":                            # register -> memory
            d = self.address(b, None)
            r = a.reg
            shifts = [24, 16, 8, 0] if t == "l" else [8, 0]
            for k, s in enumerate(shifts):
                self.e(f"bus_w8({d} + {2 * k}, (c->d[{r}] >> {s}) & 0xFF);")
        else:
            d = self.address(a, None)
            r = b.reg
            if t == "l":
                self.e(f"c->d[{r}] = bus_r8({d}) << 24 | bus_r8({d} + 2) << 16 | bus_r8({d} + 4) << 8 | bus_r8({d} + 6);")
            else:
                self.e(f"set_d(c, {r}, bus_r8({d}) << 8 | bus_r8({d} + 2), MASK_W);")
        self.finish()

    # --- BCD
    def _bcd(self, func: str):
        src, dst = self.i.ops
        if src.mode == "Dn":
            s = self.read(src, "b")
            d = self.read(dst, "b")
            r = self.tmp()
            self.e(f"uint32_t {r} = op_{func}(c, {s}, {d});")
            self.write(dst, "b", r)
        else:
            s = self.read(EA("-(An)", src.reg), "b")
            dd = self.address(EA("-(An)", dst.reg), "b")
            d = self.read(dst, "b", dd)
            r = self.tmp()
            self.e(f"uint32_t {r} = op_{func}(c, {s}, {d});")
            self.write(dst, "b", r, dd)
        self.finish()

    def op_abcd(self): self._bcd("abcd")
    def op_sbcd(self): self._bcd("sbcd")

    def op_nbcd(self):
        d, addr = self.read_modify(self.i.ops[0], "b")
        r = self.tmp()
        self.e(f"uint32_t {r} = op_sbcd(c, {d}, 0);")
        self.write(self.i.ops[0], "b", r, addr)
        self.finish()

    # --- others
    def op_chk(self):
        s = self.read(self.i.ops[0], "w")
        r = self.i.ops[1].reg
        self.e(f"{{ int32_t v = (int16_t)(c->d[{r}] & 0xFFFF), lim = (int16_t){s};")
        self.e("  c->z = (v == 0); c->v = 0; c->c = 0;")
        self.e(f"  if (v < 0) {{ c->n = 1; {self.exception(6, self.next_addr())} }}")
        self.e(f"  if (v > lim) {{ c->n = 0; {self.exception(6, self.next_addr())} }} }}")
        self.finish()

    def op_tas(self):
        d, addr = self.read_modify(self.i.ops[0], "b")
        self.e(f"(void)op_logic(c, {d}, MASK_B, MSB_B);")
        self.write(self.i.ops[0], "b", f"{d} | 0x80u", addr)
        self.finish()

    def _ccr(self, opc: str):
        v = self.i.ops[0].value & 0x1F
        self.e(f"cpu_set_ccr(c, (cpu_get_sr(c) & 0x1F) {opc} 0x{v:X}u);")
        self.finish()

    def op_andi_ccr(self): self._ccr("&")
    def op_ori_ccr(self):  self._ccr("|")
    def op_eori_ccr(self): self._ccr("^")

    def _sr(self, opc: str):
        self.privileged()
        v = self.i.ops[0].value & 0xFFFF
        self.e(f"cpu_set_sr(c, (cpu_get_sr(c) {opc} 0x{v:X}u) & 0xA71Fu);")
        self.e(f"PD_SR_CHANGED(0x{self.next_addr():X}u);")

    def op_andi_sr(self): self._sr("&")
    def op_ori_sr(self):  self._sr("|")
    def op_eori_sr(self): self._sr("^")

    def op_move_to_ccr(self):
        s = self.read(self.i.ops[0], "w")
        self.e(f"cpu_set_ccr(c, {s});")
        self.finish()

    def op_move_to_sr(self):
        self.privileged()
        s = self.read(self.i.ops[0], "w")
        self.e(f"cpu_set_sr(c, {s} & 0xA71Fu);")
        self.e(f"PD_SR_CHANGED(0x{self.next_addr():X}u);")

    def op_move_from_sr(self):
        ea = self.i.ops[0]
        if ea.is_memory():
            d = self.address(ea, "w")
            self.e(f"(void)bus_r16({d});")            # the 68000 reads before writing
            self.write(ea, "w", "cpu_get_sr(c)", d)
        else:
            self.write(ea, "w", "cpu_get_sr(c)")
        self.finish()

    def op_move_to_usp(self):
        self.privileged()
        self.e(f"c->usp = c->a[{self.i.ops[0].reg}];")
        self.finish()

    def op_move_from_usp(self):
        self.privileged()
        self.e(f"c->a[{self.i.ops[0].reg}] = c->usp;")
        self.finish()

    def op_stop(self):
        self.privileged()
        self.e(f"cpu_set_sr(c, 0x{self.i.ops[0].value & 0xA71F:X}u);")
        self.e(f"PD_STOP(0x{self.next_addr():X}u);")

    def op_reset(self):
        self.privileged()
        self.e(self.cycles(132) + "PD_EXTERNAL_RESET();")
        self.finish()


def emit(ins: Instr) -> list[str]:
    return Emitter(ins).emit()
