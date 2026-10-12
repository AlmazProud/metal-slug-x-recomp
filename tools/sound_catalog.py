"""Genera src/port/profile_sounds.c: la lista de músicas, efectos y voces de Metal Slug X para el
menú de sonido y la vibración (Fase 6B).

Entrada: los catálogos que produce el port, uno por mode del driver de sonido:
  mslugx --sin-window --frames 1300 --catalog-sonido cat_pMM.tsv   con PD_CATALOGO_PREFIJO=MM
(MM = 00 música, 10 efectos, 11 voces; el cuadro 1300 está justo después de que el juego reinicia el
driver). Solo se guardan números de command, duraciones y nombres: ningún dato de audio.

Rangos válidos (lo de afuera es el driver leyendo fuera de sus tablas):
  música 20-31, efectos 20-FF, voces 20-6E.
Nombres: solo los confirmados con screenshots de la partida grabada (docs/PHASE6_REMASTER.md); el
resto se identifica por número.

Uso: python sound_catalog.py CARPETA_CATALOGOS SALIDA.c
"""
from __future__ import annotations

import sys
from pathlib import Path

RANGOS = {"00": (0x20, 0x31), "10": (0x20, 0xFF), "11": (0x20, 0x6E)}
NOMBRES = {
    ("00", 0x20): "Mision cumplida (fanfarria)",
    ("00", 0x21): "Mision 1",
    ("00", 0x22): "Mision 2",
    ("00", 0x2C): "Combate contra el tanque",
    ("00", 0x2E): "Como jugar",
    ("00", 0x30): "Seleccion de soldado",
    ("10", 0xF7): "Pistola",
    ("10", 0x8C): "Vulcan del Slug",
    ("10", 0x84): "Canon del Slug",
    ("10", 0x22): "Explosion grande",
    ("10", 0x25): "Explosion",
    ("10", 0xF2): "Impacto en blindaje",
    ("11", 0x31): "Mission 1... start!",
    ("11", 0x37): "Mission complete!",
    ("11", 0x49): "Seleccion de soldado",
    ("11", 0x50): "Great!",
}
PREFIJO_NOMBRE = {"00": "Pista", "10": "Efecto", "11": "Voz"}


def leer(ruta: Path) -> dict[int, tuple[float, int]]:
    data = {}
    for line in ruta.read_text().splitlines():
        if line.startswith("#"):
            continue
        c = line.split("\t")
        data[int(c[0], 16)] = (float(c[1]), int(c[2]))
    return data


def main() -> int:
    carpeta, output = Path(sys.argv[1]), Path(sys.argv[2])
    filas = []
    for mode in ("00", "10", "11"):
        cat = leer(carpeta / f"cat_p{mode}.tsv")
        lo, hi = RANGOS[mode]
        for cmd in range(lo, hi + 1):
            seg, bucle = cat.get(cmd, (0.0, 0))
            if seg <= 0:
                continue
            name = NOMBRES.get((mode, cmd), f"{PREFIJO_NOMBRE[mode]} {cmd:02X}")
            tenths = 0xFFFF if bucle else min(int(round(seg * 10)), 0xFFFE)
            filas.append(f'    {{0x{mode}, 0x{cmd:02X}, {tenths}, "{name}"}},')
    text = (
        "/* Generado por tools/sound_catalog.py: no editar a mano.\n"
        " * Sonidos de Metal Slug X: mode del driver (00 música, 10 efectos, 11 voces), command,\n"
        " * duración en décimas de second (0xFFFF = en bucle) y name. */\n"
        '#include "sounds.h"\n\n'
        "const Sound PROFILE_SOUNDS[] = {\n" + "\n".join(filas) + "\n};\n"
        "const int PROFILE_SOUND_COUNT = (int)(sizeof PROFILE_SOUNDS / sizeof PROFILE_SOUNDS[0]);\n"
    )
    output.write_text(text, encoding="utf-8")
    print(f"{len(filas)} sonidos -> {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
