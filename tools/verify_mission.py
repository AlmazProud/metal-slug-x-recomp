"""Prueba de la Fase 4: reproduce en el port una partida grabada en MAME y exige cero divergencias.

Ejecuta el port sin ventana con las entradas grabadas (tools/mame/inputs.lua) y la fecha
del reloj de la grabación, escribe sus huellas y las compara con las de MAME
(tools/mame/fingerprints.lua) mediante compare_fingerprints.py.

Uso:
  python verify_mission.py --exe mslugx.exe --roms CARPETA --entradas ENTRADAS.txt
                             --referencia HUELLAS_MAME.txt --cuadros N --fecha AAAA-MM-DD-hh-mm-ss-D
                             --salida HUELLAS_PORT.txt

Cómo se generan las referencias (MAME 0.289, nvram vacía en una carpeta nueva):
  set PD_PIXELES=1 & set PD_HUELLAS=<ref> & set PD_ENTRADAS=<entradas>
  set PD_SCRIPT_ENTRADAS=<repo>/tools/mame/inputs.lua
  mame mslugx -bios us -rompath <roms>/mame -playback mision1.inp -input_directory <inp>
       -exit_after_playback -nvram_directory <vacía> -video none -sound none -nothrottle
       -autoboot_script <repo>/tools/mame/fingerprints.lua
"""
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, required=True)
    ap.add_argument("--roms", type=Path, required=True)
    ap.add_argument("--entradas", type=Path, required=True)
    ap.add_argument("--referencia", type=Path, required=True)
    ap.add_argument("--cuadros", type=int, required=True)
    ap.add_argument("--fecha", required=True)
    ap.add_argument("--salida", type=Path, required=True)
    a = ap.parse_args()

    r = subprocess.run([str(a.exe), "--roms", str(a.roms), "--sin-ventana", "--cuadros", str(a.cuadros),
                        "--entradas", str(a.entradas), "--huellas", str(a.salida), "--fecha", a.fecha],
                       capture_output=True, text=True)
    print(r.stdout.strip())
    if r.returncode != 0:
        print(r.stderr)
        return 1
    comparador = Path(__file__).resolve().parent / "compare_fingerprints.py"
    return subprocess.run([sys.executable, str(comparador), str(a.referencia), str(a.salida)]).returncode


if __name__ == "__main__":
    sys.exit(main())
