"""Cobertura dinámica del 68000 de mslugx usando MAME como oráculo.

Lanza MAME con el depurador (sin interfaz) y herramientas/mame/cobertura.lua, consume las
trazas por partes a medida que se generan y las borra, y guarda un JSON con:
  - direcciones ejecutadas por región (P1, ventana de P2 por banco, BIOS, RAM),
  - destinos observados de saltos indirectos (jmp/jsr con operando entre paréntesis).

Uso:
    python herramientas/cobertura.py --segundos 180 [--salida ARCHIVO] [--sin-bot]

El resultado (direcciones, no bytes del ROM) va por defecto a <raíz>/analisis/cobertura.json.
"""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import time
from collections import defaultdict
from pathlib import Path

LINEA = re.compile(r"^([0-9A-F]{6}): (\S+)\s*(.*)$")


def ruta_corta(p: Path) -> str:
    """Ruta 8.3 (sin espacios) para el depurador de MAME, que separa argumentos con comas/espacios."""
    import ctypes
    buf = ctypes.create_unicode_buffer(1024)
    ctypes.windll.kernel32.GetShortPathNameW(str(p), buf, 1024)
    if not buf.value or " " in buf.value:
        sys.exit(f"No hay ruta corta sin espacios para {p}")
    return buf.value


def region(pc: int) -> str:
    if pc < 0x100000:
        return "p1"
    if 0x200000 <= pc < 0x300000:
        return "p2_ventana"
    if 0xC00000 <= pc < 0xD00000:
        return "bios"
    if 0x100000 <= pc < 0x200000:
        return "ram"
    return "otro"


class Acumulador:
    def __init__(self) -> None:
        self.pcs: dict[str, set] = defaultdict(set)
        self.indirectos: set[tuple[int, int, int]] = set()   # (origen, destino, banco)
        self.banco = 0
        self.instrucciones = 0

    def procesar(self, archivo: Path) -> None:
        previo = None
        with archivo.open("r", encoding="ascii", errors="replace") as f:
            for linea in f:
                if linea.startswith("BANK "):
                    self.banco = int(linea[5:])
                    continue
                m = LINEA.match(linea)
                if not m:
                    continue
                pc = int(m.group(1), 16)
                self.instrucciones += 1
                r = region(pc)
                self.pcs[r if r != "p2_ventana" else f"p2_banco{self.banco}"].add(pc)
                if previo is not None:
                    self.indirectos.add((previo, pc, self.banco))
                    previo = None
                if m.group(2) in ("jmp", "jsr") and "(" in m.group(3):
                    previo = pc

    def resultado(self) -> dict:
        return {
            "instrucciones_trazadas": self.instrucciones,
            "pcs": {k: sorted(v) for k, v in sorted(self.pcs.items())},
            "indirectos": sorted(self.indirectos),
        }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    raiz = Path(os.environ.get("PD_ROOT", Path(__file__).resolve().parents[2]))
    ap.add_argument("--segundos", type=int, default=180, help="segundos emulados")
    ap.add_argument("--salida", type=Path, default=raiz / "analisis" / "cobertura.json")
    ap.add_argument("--sin-bot", action="store_true")
    ap.add_argument("--playback", type=Path,
                    help="reproduce una grabación .inp de MAME (ignora --segundos y desactiva el bot)")
    a = ap.parse_args()

    trabajo = raiz / "tools" / "_verificacion" / "cobertura"
    trabajo.mkdir(parents=True, exist_ok=True)
    for viejo in list(trabajo.glob("traza_*.txt")) + [trabajo / "FIN"]:
        viejo.unlink(missing_ok=True)

    mame = raiz / "tools" / "mame" / "mame.exe"
    lua = Path(__file__).resolve().parent / "mame" / "cobertura.lua"
    # Una reproducción exige el mismo estado inicial que la grabación: nvram vacía.
    nvram = trabajo / "nvram"
    if nvram.exists():
        for f in sorted(nvram.rglob("*"), reverse=True):
            f.unlink() if f.is_file() else f.rmdir()
    jugar = not (a.sin_bot or a.playback)
    env = dict(os.environ, PD_TRAZA_DIR=ruta_corta(trabajo), PD_JUGAR="1" if jugar else "0")
    cmd = [str(mame), "mslugx", "-bios", "us", "-rompath", str(raiz / "roms" / "mame"),
           "-video", "none", "-sound", "none", "-nothrottle",
           "-debug", "-debugger", "none", "-autoboot_script", str(lua),
           "-cfg_directory", str(trabajo / "cfg"), "-nvram_directory", str(nvram)]
    if a.playback:
        cmd += ["-playback", a.playback.name, "-input_directory", str(a.playback.parent),
                "-exit_after_playback"]
    else:
        cmd += ["-seconds_to_run", str(a.segundos)]
    proc = subprocess.Popen(cmd, cwd=mame.parent, env=env,
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    acc = Acumulador()
    hecho = 0
    inicio = time.time()

    def procesar_listos(final: bool) -> None:
        nonlocal hecho
        while True:
            actual = trabajo / f"traza_{hecho:04d}.txt"
            siguiente = trabajo / f"traza_{hecho + 1:04d}.txt"
            if not actual.exists() or not (final or siguiente.exists()):
                return
            acc.procesar(actual)
            actual.unlink()
            hecho += 1
            print(f"  parte {hecho:3d}  {time.time() - inicio:6.0f} s  "
                  f"p1={len(acc.pcs['p1'])} bios={len(acc.pcs['bios'])} indirectos={len(acc.indirectos)}",
                  flush=True)

    while proc.poll() is None:
        procesar_listos(False)
        time.sleep(2)
    procesar_listos(True)

    res = acc.resultado()
    lecturas = trabajo / "lecturas_p1.txt"
    if lecturas.exists():
        # Direcciones (pares) de P1 leídas por el 68000, incluidas las búsquedas de opcodes.
        res["lecturas_p1"] = sorted(int(x, 16) for x in lecturas.read_text().split())
        lecturas.unlink()
    a.salida.parent.mkdir(parents=True, exist_ok=True)
    a.salida.write_text(json.dumps(res), encoding="utf-8")
    print(f"MAME terminó con código {proc.returncode}; {acc.instrucciones} instrucciones; salida: {a.salida}")
    return 0 if proc.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
