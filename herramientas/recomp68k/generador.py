"""Generador de C a partir de instrucciones 68000 decodificadas (decodificador.py).

Traduce una instrucción a sentencias C que operan sobre `Cpu68k *c` (src/cpu/cpu68k.h).
El control de flujo se expresa con macros que define quien incluye el código generado:

    PD_SALTO(destino)      salto a una dirección conocida
    PD_SALTO_DIN(expr)     salto a una dirección calculada en tiempo de ejecución
    PD_SIGUIENTE(dir)      continuar con la instrucción siguiente (dir = su dirección)
    PD_EXCEPCION(vector, pc_retorno)

Así el mismo generador sirve para las pruebas diferenciales (una función por instrucción)
y para el port (funciones completas con etiquetas).

El código C generado se deriva del programa del juego: se genera en tiempo de compilación a
partir de los ROMs del usuario y nunca se versiona.
"""
from __future__ import annotations

from decodificador import EA, Instr, TAMAÑO_BYTES

MASK = {"b": "MASK_B", "w": "MASK_W", "l": "MASK_L"}
MSB = {"b": "MSB_B", "w": "MSB_W", "l": "MSB_L"}
CC_C = {"t": "CC_T", "f": "CC_F", "hi": "CC_HI", "ls": "CC_LS", "cc": "CC_CC", "cs": "CC_CS",
        "ne": "CC_NE", "eq": "CC_EQ", "vc": "CC_VC", "vs": "CC_VS", "pl": "CC_PL", "mi": "CC_MI",
        "ge": "CC_GE", "lt": "CC_LT", "gt": "CC_GT", "le": "CC_LE"}


# Ciclos de las excepciones del 68000 (tabla de Musashi 4.60 para el 68000, MIT).
CICLOS_EXCEPCION = {4: 34, 5: 38, 6: 40, 7: 34, 8: 34, **{32 + n: 34 for n in range(16)}}

# Ciclos base por opcode (tests/diferencial/volcar_ciclos.c -> archivo de 65536 bytes).
CICLOS_BASE: bytes | None = None


def cargar_ciclos(ruta) -> None:
    global CICLOS_BASE
    with open(ruta, "rb") as f:
        CICLOS_BASE = f.read()
    assert len(CICLOS_BASE) == 0x10000


class NoSoportada(Exception):
    pass


def _rd(tam: str) -> str:
    return {"b": "bus_r8", "w": "bus_r16", "l": "bus_r32"}[tam]


def _wr(tam: str) -> str:
    return {"b": "bus_w8", "w": "bus_w16", "l": "bus_w32"}[tam]


class Emisor:
    """Emite C para una instrucción. `lineas` acumula las sentencias."""

    def __init__(self, ins: Instr):
        self.i = ins
        self.lineas: list[str] = []
        self.n = 0

    # ------------------------------------------------------------ utilidades
    def tmp(self, prefijo="t") -> str:
        self.n += 1
        return f"{prefijo}{self.n}"

    def e(self, s: str) -> None:
        self.lineas.append(s)

    def sig(self) -> int:
        return (self.i.dir + self.i.largo) & 0xFFFFFF

    def indice(self, ea: EA) -> str:
        r = f"c->a[{ea.xi & 7}]" if ea.xi >= 8 else f"c->d[{ea.xi & 7}]"
        return r if ea.xi_largo else f"sext16({r} & 0xFFFF)"

    def paso(self, ea: EA, tam: str) -> int:
        n = TAMAÑO_BYTES[tam]
        return 2 if (n == 1 and ea.reg == 7) else n

    def direccion(self, ea: EA, tam: str | None) -> str:
        """Calcula la dirección efectiva (con efectos de (An)+ / -(An)) y devuelve una variable."""
        v = self.tmp("ea")
        m = ea.modo
        if m == "(An)":
            self.e(f"uint32_t {v} = c->a[{ea.reg}];")
        elif m == "(An)+":
            self.e(f"uint32_t {v} = c->a[{ea.reg}]; c->a[{ea.reg}] += {self.paso(ea, tam)};")
        elif m == "-(An)":
            self.e(f"c->a[{ea.reg}] -= {self.paso(ea, tam)}; uint32_t {v} = c->a[{ea.reg}];")
        elif m == "d16(An)":
            self.e(f"uint32_t {v} = c->a[{ea.reg}] + (uint32_t)({ea.disp});")
        elif m == "d8(An,Xi)":
            self.e(f"uint32_t {v} = c->a[{ea.reg}] + (uint32_t)({ea.disp}) + {self.indice(ea)};")
        elif m in ("abs.w", "abs.l"):
            self.e(f"uint32_t {v} = 0x{ea.valor & 0xFFFFFFFF:X}u;")
        elif m == "d16(PC)":
            self.e(f"uint32_t {v} = 0x{(ea.pc_ext + ea.disp) & 0xFFFFFFFF:X}u;")
        elif m == "d8(PC,Xi)":
            self.e(f"uint32_t {v} = 0x{(ea.pc_ext + ea.disp) & 0xFFFFFFFF:X}u + {self.indice(ea)};")
        else:
            raise NoSoportada(f"dirección de {m}")
        return v

    def leer(self, ea: EA, tam: str, dir_var: str | None = None) -> str:
        """Lee un operando; para memoria, `dir_var` permite reutilizar una dirección ya calculada."""
        v = self.tmp("v")
        m = ea.modo
        if m == "Dn":
            self.e(f"uint32_t {v} = c->d[{ea.reg}] & {MASK[tam]};")
        elif m == "An":
            self.e(f"uint32_t {v} = c->a[{ea.reg}] & {MASK[tam]};")
        elif m == "#imm":
            self.e(f"uint32_t {v} = 0x{ea.valor & 0xFFFFFFFF:X}u;")
        else:
            d = dir_var or self.direccion(ea, tam)
            self.e(f"uint32_t {v} = {_rd(tam)}({d});")
        return v

    def escribir(self, ea: EA, tam: str, valor: str, dir_var: str | None = None) -> None:
        m = ea.modo
        if m == "Dn":
            self.e(f"set_d(c, {ea.reg}, {valor}, {MASK[tam]});")
        elif m == "An":
            self.e(f"c->a[{ea.reg}] = {valor};")
        else:
            d = dir_var or self.direccion(ea, tam)
            self.e(f"{_wr(tam)}({d}, {valor});")

    def leer_modificar(self, ea: EA, tam: str):
        """Para operaciones lectura-modificación-escritura: calcula la dirección una sola vez."""
        if ea.es_memoria():
            d = self.direccion(ea, tam)
            return self.leer(ea, tam, d), d
        return self.leer(ea, tam), None

    def privilegiada(self) -> None:
        self.e(f"if (!c->s) {{ {self.excepcion(8, self.i.dir)} }}")

    # ------------------------------------------------------------ emisión
    def emitir(self) -> list[str]:
        i = self.i
        f = getattr(self, "op_" + i.op, None)
        if f is None:
            raise NoSoportada(i.op)
        if CICLOS_BASE is not None:
            self.e(f"c->ciclos += {self.base()};")
        f()
        return self.lineas

    def base(self) -> int:
        return CICLOS_BASE[self.i.opcode] if CICLOS_BASE is not None else 0

    def ciclos(self, n) -> str:
        """Sentencia que suma `n` ciclos (texto C o entero); vacía si no hay tabla de ciclos."""
        return f"c->ciclos += {n}; " if CICLOS_BASE is not None else ""

    def excepcion(self, vector: int, pc: int) -> str:
        """Sentencia de excepción: su costo reemplaza al de la instrucción."""
        return f"{self.ciclos(CICLOS_EXCEPCION[vector] - self.base())}PD_EXCEPCION({vector}, 0x{pc:X}u);"

    def fin(self) -> None:
        self.e(f"PD_SIGUIENTE(0x{self.sig():X}u);")

    # --- movimientos
    def op_move(self):
        t = self.i.tam
        s = self.leer(self.i.ops[0], t)
        r = self.tmp()
        self.e(f"uint32_t {r} = op_logic(c, {s}, {MASK[t]}, {MSB[t]});")
        self.escribir(self.i.ops[1], t, r)
        self.fin()

    def op_moveq(self):
        self.e(f"c->d[{self.i.ops[1].reg}] = op_logic(c, 0x{self.i.ops[0].valor:X}u, MASK_L, MSB_L);")
        self.fin()

    def op_movea(self):
        t = self.i.tam
        s = self.leer(self.i.ops[0], t)
        self.e(f"c->a[{self.i.ops[1].reg}] = {'sext16(' + s + ')' if t == 'w' else s};")
        self.fin()

    def op_lea(self):
        d = self.direccion(self.i.ops[0], None)
        self.e(f"c->a[{self.i.ops[1].reg}] = {d};")
        self.fin()

    def op_pea(self):
        d = self.direccion(self.i.ops[0], None)
        self.e(f"c->a[7] -= 4; bus_w32(c->a[7], {d});")
        self.fin()

    def op_clr(self):
        t = self.i.tam
        ea = self.i.ops[0]
        if ea.es_memoria():
            d = self.direccion(ea, t)
            self.e(f"(void){_rd(t)}({d});")      # el 68000 lee antes de escribir en CLR
            self.escribir(ea, t, "0", d)
        else:
            self.escribir(ea, t, "0")
        self.e("c->n = 0; c->z = 1; c->v = 0; c->c = 0;")
        self.fin()

    def op_tst(self):
        t = self.i.tam
        s = self.leer(self.i.ops[0], t)
        self.e(f"(void)op_logic(c, {s}, {MASK[t]}, {MSB[t]});")
        self.fin()

    def op_exg(self):
        a, b = self.i.ops
        ra = f"c->{'d' if a.modo == 'Dn' else 'a'}[{a.reg}]"
        rb = f"c->{'d' if b.modo == 'Dn' else 'a'}[{b.reg}]"
        t = self.tmp()
        self.e(f"uint32_t {t} = {ra}; {ra} = {rb}; {rb} = {t};")
        self.fin()

    def op_swap(self):
        r = self.i.ops[0].reg
        self.e(f"c->d[{r}] = op_logic(c, c->d[{r}] >> 16 | c->d[{r}] << 16, MASK_L, MSB_L);")
        self.fin()

    def op_ext(self):
        r = self.i.ops[0].reg
        if self.i.tam == "w":
            self.e(f"set_d(c, {r}, op_logic(c, sext8(c->d[{r}] & 0xFF), MASK_W, MSB_W), MASK_W);")
        else:
            self.e(f"c->d[{r}] = op_logic(c, sext16(c->d[{r}] & 0xFFFF), MASK_L, MSB_L);")
        self.fin()

    # --- aritmética y lógica binaria
    def _binaria(self, funcion: str, escribe=True):
        t = self.i.tam
        src, dst = self.i.ops
        s = self.leer(src, t)
        d, dir_ = self.leer_modificar(dst, t)
        r = self.tmp()
        if funcion == "cmp":
            self.e(f"op_cmp(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
        elif funcion in ("and", "or", "eor"):
            opc = {"and": "&", "or": "|", "eor": "^"}[funcion]
            self.e(f"uint32_t {r} = op_logic(c, {d} {opc} {s}, {MASK[t]}, {MSB[t]});")
        else:
            self.e(f"uint32_t {r} = op_{funcion}(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
        if escribe and funcion != "cmp":
            self.escribir(dst, t, r, dir_)
        self.fin()

    def op_add(self):  self._binaria("add")
    def op_addi(self): self._binaria("add")
    def op_sub(self):  self._binaria("sub")
    def op_subi(self): self._binaria("sub")
    def op_cmp(self):  self._binaria("cmp")
    def op_cmpi(self): self._binaria("cmp")
    def op_and(self):  self._binaria("and")
    def op_andi(self): self._binaria("and")
    def op_or(self):   self._binaria("or")
    def op_ori(self):  self._binaria("or")
    def op_eor(self):  self._binaria("eor")
    def op_eori(self): self._binaria("eor")

    def _quick(self, funcion: str):
        src, dst = self.i.ops
        if dst.modo == "An":                         # ADDQ/SUBQ a An: 32 bits, sin flags
            signo = "+" if funcion == "add" else "-"
            self.e(f"c->a[{dst.reg}] {signo}= {src.valor};")
            self.fin()
            return
        self._binaria(funcion)

    def op_addq(self): self._quick("add")
    def op_subq(self): self._quick("sub")

    def _arit_a(self, signo: str):
        t = self.i.tam
        s = self.leer(self.i.ops[0], t)
        v = f"sext16({s})" if t == "w" else s
        self.e(f"c->a[{self.i.ops[1].reg}] {signo}= {v};")
        self.fin()

    def op_adda(self): self._arit_a("+")
    def op_suba(self): self._arit_a("-")

    def op_cmpa(self):
        t = self.i.tam
        s = self.leer(self.i.ops[0], t)
        v = f"sext16({s})" if t == "w" else s
        self.e(f"op_cmp(c, {v}, c->a[{self.i.ops[1].reg}], MASK_L, MSB_L);")
        self.fin()

    def op_cmpm(self):
        t = self.i.tam
        s = self.leer(self.i.ops[0], t)
        d = self.leer(self.i.ops[1], t)
        self.e(f"op_cmp(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
        self.fin()

    def _extendida(self, funcion: str):
        t = self.i.tam
        src, dst = self.i.ops
        if src.modo == "Dn":
            s = self.leer(src, t)
            d = self.leer(dst, t)
            r = self.tmp()
            self.e(f"uint32_t {r} = op_{funcion}(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
            self.escribir(dst, t, r)
        else:                                         # -(Ay),-(Ax)
            s = self.leer(EA("-(An)", src.reg), t)
            dd = self.direccion(EA("-(An)", dst.reg), t)
            d = self.leer(dst, t, dd)
            r = self.tmp()
            self.e(f"uint32_t {r} = op_{funcion}(c, {s}, {d}, {MASK[t]}, {MSB[t]});")
            self.escribir(dst, t, r, dd)
        self.fin()

    def op_addx(self): self._extendida("addx")
    def op_subx(self): self._extendida("subx")

    def _unaria(self, expr: str):
        t = self.i.tam
        d, dir_ = self.leer_modificar(self.i.ops[0], t)
        r = self.tmp()
        self.e(f"uint32_t {r} = {expr.format(d=d, M=MASK[t], S=MSB[t])};")
        self.escribir(self.i.ops[0], t, r, dir_)
        self.fin()

    def op_neg(self):  self._unaria("op_neg(c, {d}, {M}, {S})")
    def op_negx(self): self._unaria("op_negx(c, {d}, {M}, {S})")
    def op_not(self):  self._unaria("op_logic(c, ~{d}, {M}, {S})")

    # --- control de flujo
    def op_bra(self):
        self.e(f"PD_SALTO(0x{self.i.destino:X}u);")

    def op_bcc(self):
        self.e(f"if (cond(c, {CC_C[self.i.cc]})) {{ PD_SALTO(0x{self.i.destino:X}u); }}")
        self.e(self.ciclos(2 if self.i.largo == 4 else -2))
        self.fin()

    def op_bsr(self):
        self.e(f"c->a[7] -= 4; bus_w32(c->a[7], 0x{self.sig():X}u);")
        self.e(f"PD_LLAMADA(0x{self.i.destino:X}u, 0x{self.sig():X}u);")

    def op_jsr(self):
        d = self.direccion(self.i.ops[0], None)
        self.e(f"c->a[7] -= 4; bus_w32(c->a[7], 0x{self.sig():X}u);")
        self.e(f"PD_LLAMADA_DIN({d}, 0x{self.sig():X}u);")

    def op_jmp(self):
        d = self.direccion(self.i.ops[0], None)
        self.e(f"PD_SALTO_DIN({d});")

    def op_rts(self):
        t = self.tmp()
        self.e(f"uint32_t {t} = bus_r32(c->a[7]); c->a[7] += 4;")
        self.e(f"PD_RETORNO({t});")

    def op_rtr(self):
        t = self.tmp()
        self.e(f"cpu_set_ccr(c, bus_r16(c->a[7])); c->a[7] += 2;")
        self.e(f"uint32_t {t} = bus_r32(c->a[7]); c->a[7] += 4;")
        self.e(f"PD_RETORNO({t});")

    def op_rte(self):
        self.privilegiada()
        sr, pc = self.tmp(), self.tmp()
        self.e(f"uint32_t {sr} = bus_r16(c->a[7]); uint32_t {pc} = bus_r32(c->a[7] + 2); c->a[7] += 6;")
        self.e(f"cpu_set_sr(c, {sr});")
        self.e(f"PD_SALTO_DIN({pc});")

    def op_dbcc(self):
        r = self.i.ops[0].reg
        self.e(f"if (!cond(c, {CC_C[self.i.cc]})) {{")
        self.e(f"    uint32_t cnt = (c->d[{r}] - 1) & 0xFFFF; set_d(c, {r}, cnt, MASK_W);")
        self.e(f"    if (cnt != 0xFFFF) {{ {self.ciclos(-2)}PD_SALTO(0x{self.i.destino:X}u); }}")
        self.e(f"    {self.ciclos(2)}")
        self.e("}")
        self.fin()

    def op_scc(self):
        ea = self.i.ops[0]
        v = f"(cond(c, {CC_C[self.i.cc]}) ? 0xFFu : 0u)"
        if ea.es_memoria():
            d = self.direccion(ea, "b")
            self.e(f"(void)bus_r8({d});")             # Scc lee antes de escribir
            self.escribir(ea, "b", v, d)
        else:
            self.escribir(ea, "b", v)
            if self.i.cc != "t":                  # ST Dn ya trae su costo completo en la tabla base
                self.e(f"if (cond(c, {CC_C[self.i.cc]})) {{ {self.ciclos(2)}}}")
        self.fin()

    def op_nop(self):
        self.fin()

    def op_trap(self):
        self.e(self.excepcion(32 + self.i.dato, self.sig()))

    def op_trapv(self):
        self.e(f"if (c->v) {{ {self.excepcion(7, self.sig())} }}")
        self.fin()

    def op_illegal(self):
        self.e(self.excepcion(4, self.i.dir))

    def op_link(self):
        r = self.i.ops[0].reg
        self.e(f"c->a[7] -= 4; bus_w32(c->a[7], c->a[{r}]); c->a[{r}] = c->a[7]; c->a[7] += (uint32_t)({self.i.dato});")
        self.fin()

    def op_unlk(self):
        r = self.i.ops[0].reg
        self.e(f"c->a[7] = c->a[{r}]; c->a[{r}] = bus_r32(c->a[7]); c->a[7] += 4;")
        self.fin()


    # --- bits
    def _bit(self, accion: str):
        src, dst = self.i.ops
        t = "l" if dst.modo == "Dn" else "b"
        modulo = 31 if t == "l" else 7
        if src.modo == "#imm":
            nbit = f"{src.valor & modulo}u"
        else:
            nbit = f"(c->d[{src.reg}] & {modulo}u)"
        d, dir_ = self.leer_modificar(dst, t)
        m = self.tmp("m")
        self.e(f"uint32_t {m} = 1u << {nbit}; c->z = ({d} & {m}) == 0;")
        if accion != "btst":
            expr = {"bchg": f"{d} ^ {m}", "bclr": f"{d} & ~{m}", "bset": f"{d} | {m}"}[accion]
            self.escribir(dst, t, expr, dir_)
        self.fin()

    def op_btst(self): self._bit("btst")
    def op_bchg(self): self._bit("bchg")
    def op_bclr(self): self._bit("bclr")
    def op_bset(self): self._bit("bset")

    # --- desplazamientos y rotaciones
    def _shift(self, tipo: str, izq: bool):
        t = self.i.tam
        if len(self.i.ops) == 1:                      # en memoria: una posición, .w
            d, dir_ = self.leer_modificar(self.i.ops[0], "w")
            r = self.tmp()
            self.e(f"uint32_t {r} = op_shift(c, SH_{tipo.upper()}, {int(izq)}, 1, {d}, MASK_W, MSB_W);")
            self.escribir(self.i.ops[0], "w", r, dir_)
        else:
            cnt_ea, dst = self.i.ops
            cnt = f"{cnt_ea.valor}u" if cnt_ea.modo == "#imm" else f"(c->d[{cnt_ea.reg}] & 63u)"
            self.e(self.ciclos(f"2 * {cnt}"))
            r = self.tmp()
            self.e(f"uint32_t {r} = op_shift(c, SH_{tipo.upper()}, {int(izq)}, {cnt}, "
                   f"c->d[{dst.reg}] & {MASK[t]}, {MASK[t]}, {MSB[t]});")
            self.escribir(dst, t, r)
        self.fin()

    def op_asl(self):  self._shift("as", True)
    def op_asr(self):  self._shift("as", False)
    def op_lsl(self):  self._shift("ls", True)
    def op_lsr(self):  self._shift("ls", False)
    def op_roxl(self): self._shift("rox", True)
    def op_roxr(self): self._shift("rox", False)
    def op_rol(self):  self._shift("ro", True)
    def op_ror(self):  self._shift("ro", False)

    # --- multiplicación y división
    def _mul(self, con_signo: bool):
        s = self.leer(self.i.ops[0], "w")
        r = self.i.ops[1].reg
        if CICLOS_BASE is not None:
            self.e(f"c->ciclos += {'ciclos_muls' if con_signo else 'ciclos_mulu'}({s});")
        if con_signo:
            expr = f"(uint32_t)((int32_t)(int16_t){s} * (int32_t)(int16_t)(c->d[{r}] & 0xFFFF))"
        else:
            expr = f"{s} * (c->d[{r}] & 0xFFFF)"
        self.e(f"c->d[{r}] = op_logic(c, {expr}, MASK_L, MSB_L);")
        self.fin()

    def op_mulu(self): self._mul(False)
    def op_muls(self): self._mul(True)

    def _div(self, con_signo: bool):
        s = self.leer(self.i.ops[0], "w")
        r = self.i.ops[1].reg
        self.e(f"if ({s} == 0) {{ {self.excepcion(5, self.sig())} }}")
        self.e(f"op_div{'s' if con_signo else 'u'}(c, {r}, {s});")
        self.fin()

    def op_divu(self): self._div(False)
    def op_divs(self): self._div(True)

    # --- MOVEM / MOVEP
    def _regs(self, mascara: int) -> list[str]:
        return [f"c->{'d' if n < 8 else 'a'}[{n & 7}]" for n in range(16) if mascara >> n & 1]

    def op_movem_carga(self):
        t = self.i.tam
        ea = self.i.ops[0]
        n = TAMAÑO_BYTES[t]
        self.e(self.ciclos(bin(self.i.lista).count("1") * (8 if t == "l" else 4)))
        if ea.modo == "(An)+":
            p = self.tmp("p")
            self.e(f"uint32_t {p} = c->a[{ea.reg}];")
        else:
            p = self.direccion(ea, t)
        for reg in self._regs(self.i.lista):
            v = f"sext16(bus_r16({p}))" if t == "w" else f"bus_r32({p})"
            self.e(f"{reg} = {v}; {p} += {n};")
        if ea.modo == "(An)+":
            self.e(f"c->a[{ea.reg}] = {p};")
        self.fin()

    def op_movem_guarda(self):
        t = self.i.tam
        ea = self.i.ops[0]
        n = TAMAÑO_BYTES[t]
        self.e(self.ciclos(bin(self.i.lista).count("1") * (8 if t == "l" else 4)))
        regs = self._regs(self.i.lista)
        cop = self.tmp("r")
        self.e(f"uint32_t {cop}[16]; memcpy({cop}, c->d, 32); memcpy({cop} + 8, c->a, 32);")
        idx = [k for k in range(16) if self.i.lista >> k & 1]
        if ea.modo == "-(An)":
            p = self.tmp("p")
            self.e(f"uint32_t {p} = c->a[{ea.reg}];")
            for k in reversed(idx):
                self.e(f"{p} -= {n}; {_wr(t)}({p}, {cop}[{k}]);")
            self.e(f"c->a[{ea.reg}] = {p};")
        else:
            p = self.direccion(ea, t)
            for k in idx:
                self.e(f"{_wr(t)}({p}, {cop}[{k}]); {p} += {n};")
        del regs
        self.fin()

    def op_movep(self):
        t = self.i.tam
        a, b = self.i.ops
        if a.modo == "Dn":                            # registro -> memoria
            d = self.direccion(b, None)
            r = a.reg
            desplaz = [24, 16, 8, 0] if t == "l" else [8, 0]
            for k, s in enumerate(desplaz):
                self.e(f"bus_w8({d} + {2 * k}, (c->d[{r}] >> {s}) & 0xFF);")
        else:
            d = self.direccion(a, None)
            r = b.reg
            if t == "l":
                self.e(f"c->d[{r}] = bus_r8({d}) << 24 | bus_r8({d} + 2) << 16 | bus_r8({d} + 4) << 8 | bus_r8({d} + 6);")
            else:
                self.e(f"set_d(c, {r}, bus_r8({d}) << 8 | bus_r8({d} + 2), MASK_W);")
        self.fin()

    # --- BCD
    def _bcd(self, funcion: str):
        src, dst = self.i.ops
        if src.modo == "Dn":
            s = self.leer(src, "b")
            d = self.leer(dst, "b")
            r = self.tmp()
            self.e(f"uint32_t {r} = op_{funcion}(c, {s}, {d});")
            self.escribir(dst, "b", r)
        else:
            s = self.leer(EA("-(An)", src.reg), "b")
            dd = self.direccion(EA("-(An)", dst.reg), "b")
            d = self.leer(dst, "b", dd)
            r = self.tmp()
            self.e(f"uint32_t {r} = op_{funcion}(c, {s}, {d});")
            self.escribir(dst, "b", r, dd)
        self.fin()

    def op_abcd(self): self._bcd("abcd")
    def op_sbcd(self): self._bcd("sbcd")

    def op_nbcd(self):
        d, dir_ = self.leer_modificar(self.i.ops[0], "b")
        r = self.tmp()
        self.e(f"uint32_t {r} = op_sbcd(c, {d}, 0);")
        self.escribir(self.i.ops[0], "b", r, dir_)
        self.fin()

    # --- otras
    def op_chk(self):
        s = self.leer(self.i.ops[0], "w")
        r = self.i.ops[1].reg
        self.e(f"{{ int32_t v = (int16_t)(c->d[{r}] & 0xFFFF), lim = (int16_t){s};")
        self.e("  c->z = (v == 0); c->v = 0; c->c = 0;")
        self.e(f"  if (v < 0) {{ c->n = 1; {self.excepcion(6, self.sig())} }}")
        self.e(f"  if (v > lim) {{ c->n = 0; {self.excepcion(6, self.sig())} }} }}")
        self.fin()

    def op_tas(self):
        d, dir_ = self.leer_modificar(self.i.ops[0], "b")
        self.e(f"(void)op_logic(c, {d}, MASK_B, MSB_B);")
        self.escribir(self.i.ops[0], "b", f"{d} | 0x80u", dir_)
        self.fin()

    def _ccr(self, opc: str):
        v = self.i.ops[0].valor & 0x1F
        self.e(f"cpu_set_ccr(c, (cpu_get_sr(c) & 0x1F) {opc} 0x{v:X}u);")
        self.fin()

    def op_andi_ccr(self): self._ccr("&")
    def op_ori_ccr(self):  self._ccr("|")
    def op_eori_ccr(self): self._ccr("^")

    def _sr(self, opc: str):
        self.privilegiada()
        v = self.i.ops[0].valor & 0xFFFF
        self.e(f"cpu_set_sr(c, (cpu_get_sr(c) {opc} 0x{v:X}u) & 0xA71Fu);")
        self.e(f"PD_SR_CAMBIADO(0x{self.sig():X}u);")

    def op_andi_sr(self): self._sr("&")
    def op_ori_sr(self):  self._sr("|")
    def op_eori_sr(self): self._sr("^")

    def op_move_to_ccr(self):
        s = self.leer(self.i.ops[0], "w")
        self.e(f"cpu_set_ccr(c, {s});")
        self.fin()

    def op_move_to_sr(self):
        self.privilegiada()
        s = self.leer(self.i.ops[0], "w")
        self.e(f"cpu_set_sr(c, {s} & 0xA71Fu);")
        self.e(f"PD_SR_CAMBIADO(0x{self.sig():X}u);")

    def op_move_from_sr(self):
        ea = self.i.ops[0]
        if ea.es_memoria():
            d = self.direccion(ea, "w")
            self.e(f"(void)bus_r16({d});")            # el 68000 lee antes de escribir
            self.escribir(ea, "w", "cpu_get_sr(c)", d)
        else:
            self.escribir(ea, "w", "cpu_get_sr(c)")
        self.fin()

    def op_move_to_usp(self):
        self.privilegiada()
        self.e(f"c->usp = c->a[{self.i.ops[0].reg}];")
        self.fin()

    def op_move_from_usp(self):
        self.privilegiada()
        self.e(f"c->a[{self.i.ops[0].reg}] = c->usp;")
        self.fin()

    def op_stop(self):
        self.privilegiada()
        self.e(f"cpu_set_sr(c, 0x{self.i.ops[0].valor & 0xA71F:X}u);")
        self.e(f"PD_STOP(0x{self.sig():X}u);")

    def op_reset(self):
        self.privilegiada()
        self.e(self.ciclos(132) + "PD_RESET_EXTERNO();")
        self.fin()


def emitir(ins: Instr) -> list[str]:
    return Emisor(ins).emitir()
