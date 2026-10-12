"""Compara las huellas por cuadro del port con las de MAME (Fase 4).

Formatos (tools/mame/fingerprints.lua y la opción --huellas del port):
  V <n> <hash RAM> <hash paleta>   en cada reconocimiento de la VBlank (n = orden desde el encendido)
  F <n> <hash imagen>              imagen del cuadro n

Notas:
  - Las huellas V se toman al entrar al manejador de la VBlank, donde ambos lados están en el
    límite de una instrucción.
  - screen:pixels() en el notificador de cuadro de MAME entrega la imagen del cuadro anterior:
    la F <n> del port se compara con la F <n + 1> de MAME.

Uso:
  python compare_fingerprints.py MAME.txt PORT.txt
Devuelve 0 si todo coincide; 1 si hay diferencias (las lista).
"""
from __future__ import annotations

import sys
from pathlib import Path


def leer(ruta: Path) -> tuple[dict[int, str], dict[int, str]]:
    v, f = {}, {}
    for linea in ruta.read_text(encoding="utf-8", errors="replace").splitlines():
        p = linea.split()
        if len(p) >= 4 and p[0] == "V":
            v[int(p[1])] = p[2] + " " + p[3]
        elif len(p) >= 3 and p[0] == "F":
            f[int(p[1])] = p[2]
    return v, f


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    mv, mf = leer(Path(sys.argv[1]))
    pv, pf = leer(Path(sys.argv[2]))
    comunes_v = sorted(set(mv) & set(pv))
    dif_v = [n for n in comunes_v if mv[n] != pv[n]]
    comunes_f = sorted(n for n in pf if n + 1 in mf)
    dif_f = [n for n in comunes_f if pf[n] != mf[n + 1]]
    print(f"VBlank (RAM + paleta): {len(comunes_v)} comparadas, {len(dif_v)} distintas"
          f" (MAME {len(mv)}, port {len(pv)})")
    print(f"Imagen por cuadro:     {len(comunes_f)} comparadas, {len(dif_f)} distintas")
    if dif_v:
        print("  primeras VBlank distintas:", dif_v[:10])
    if dif_f:
        print("  primeros cuadros distintos:", dif_f[:10])
    ok = not dif_v and not dif_f and len(comunes_v) == len(mv) and len(comunes_f) >= len(mf) - 2
    print("SIN DIVERGENCIAS" if ok else "HAY DIVERGENCIAS")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
