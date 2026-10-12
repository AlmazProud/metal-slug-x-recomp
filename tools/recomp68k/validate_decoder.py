"""Compara el decodificador propio con Capstone sobre todas las instrucciones del listado de P1.

Compara longitud en bytes y mnemónico base. Las diferencias se agrupan por tipo para revisarlas
a mano: puede equivocarse cualquiera de los dos (Capstone decodifica algunos formatos del 68020
incluso en mode 68000).

Uso: python tools/recomp68k/validate_decoder.py
"""
from __future__ import annotations

import os
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from decoder import decodificar  # noqa: E402

RAIZ = Path(os.environ.get("PD_ROOT", Path(__file__).resolve().parents[3]))


def nombre_capstone(i) -> str:
    """Nuestro name canónico expresado como lo escribe Capstone."""
    if i.op == "bcc":
        return "b" + i.cc
    if i.op == "dbcc":
        return "dbra" if i.cc == "f" else "db" + i.cc
    if i.op == "scc":
        return "s" + i.cc
    if i.op.startswith("movem"):
        return "movem"
    if i.op.endswith("_ccr") or i.op.endswith("_sr"):
        return i.op.split("_")[0]
    if i.op in ("move_to_ccr", "move_to_sr", "move_from_sr", "move_to_usp", "move_from_usp"):
        return "move"
    return i.op


def main() -> int:
    raw = (RAIZ / "roms" / "mslugx_game_m68k").read_bytes()[:1 << 20]
    cpu = bytearray(len(raw))
    cpu[0::2], cpu[1::2] = raw[1::2], raw[0::2]
    leer16 = lambda a: int.from_bytes(cpu[a:a + 2], "big") if a + 2 <= len(cpu) else 0  # noqa: E731

    import capstone
    cs = capstone.Cs(capstone.CS_ARCH_M68K, capstone.CS_MODE_BIG_ENDIAN | capstone.CS_MODE_M68K_000)
    total = 0
    distintos = Counter()
    ejemplos = {}
    for line in (RAIZ / "analisis" / "listing_p1.txt").read_text(encoding="utf-8").splitlines():
        m = re.match(r"^([0-9A-F]{6})[* ] (\S+)", line)
        if not m:
            continue
        dir_ = int(m.group(1), 16)
        cap = m.group(2).split(".")[0]
        total += 1
        i = decodificar(leer16, dir_)
        largo_cap = next(cs.disasm(bytes(cpu[dir_:dir_ + 10]), dir_, count=1)).size
        if i is None:
            clave = f"nuestro: invalido / capstone: {cap}"
        elif nombre_capstone(i) != cap:
            clave = f"name: nuestro {nombre_capstone(i)} / capstone {cap}"
        elif i.largo != largo_cap:
            clave = f"largo: nuestro {i.largo} / capstone {largo_cap} ({cap})"
        else:
            continue
        distintos[clave] += 1
        ejemplos.setdefault(clave, f"{dir_:06X} {line[8:]}")
    # longitudes: se comparan con la siguiente dirección del listado cuando es contigua
    print(f"instrucciones comparadas: {total}")
    print(f"coinciden en mnemónico: {total - sum(distintos.values())}")
    for k, n in distintos.most_common(40):
        print(f"  {n:6d}  {k:<55} ej. {ejemplos[k]}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
