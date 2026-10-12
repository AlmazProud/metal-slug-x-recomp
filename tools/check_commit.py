"""Hook pre-commit: refuerza la regla 1 (el repo nunca contiene ROMs ni data extraídos).

Rechaza el commit si algún archivo preparado:
  - es binario (contiene bytes NUL): el repo es solo de text;
  - pesa más de 1 MB;
  - tiene el mismo SHA256 que cualquier archivo bajo la carpeta de ROMs del usuario
    (incluye el set reconstruido en roms/mame).
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import sys
from pathlib import Path

LIMITE = 1 << 20


def git(*args: str) -> bytes:
    return subprocess.run(["git", *args], capture_output=True, check=True).stdout


def hashes_de_roms(carpeta: Path) -> dict[str, str]:
    h = {}
    if carpeta.is_dir():
        for f in carpeta.rglob("*"):
            if f.is_file():
                h[hashlib.sha256(f.read_bytes()).hexdigest()] = str(f.relative_to(carpeta))
    return h


def main() -> int:
    raiz_repo = Path(git("rev-parse", "--show-toplevel").decode().strip())
    roms = Path(os.environ.get("PD_ROMS") or raiz_repo.parent / "roms")
    prohibidos = hashes_de_roms(roms)

    nombres = [n for n in git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z").decode().split("\0") if n]
    problemas = []
    for n in nombres:
        data = git("show", f":{n}")
        if b"\0" in data:
            problemas.append(f"{n}: es binario")
        if len(data) > LIMITE:
            problemas.append(f"{n}: pesa {len(data)} bytes (limite {LIMITE})")
        coincide = prohibidos.get(hashlib.sha256(data).hexdigest())
        if coincide:
            problemas.append(f"{n}: identico a roms/{coincide}")

    if problemas:
        print("pre-commit: RECHAZADO (regla 1: el repo nunca contiene ROMs ni data extraidos)")
        for p in problemas:
            print("   " + p)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
