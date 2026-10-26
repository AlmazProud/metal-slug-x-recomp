"""Builds a portable copy of the games for personal use (e.g. a USB drive): nothing is installed
and nothing is written to the computer it runs on.

Usage:
  python tools/make_portable.py --out E:\\ [--build repo/build] [--mslug <root>/roms]
                                [--mslug4 <root>/../MS4_Port/roms] [--mslug5 <root>/../MS5_Port/roms]
                                [--mslug3 <root>/../MS3_Port/roms] [--mslugx <root>/../roms]

For each game whose executable is built and whose files are found, <out>/Metal Slug/<game>/ gets
the executable, SDL3.dll, the licenses, a "game" folder with the user's own copy of the game files
(copied from the given folder: the DotEmu-layout files only, not MAME sets; for Metal Slug 5, the
decrypted reference files made from the user's MAME set; for Metal Slug 4, GOG's files and the
system files, or with --mame-set or without GOG's files the MAME set) and an empty "save" folder. With "save"
next to the executable the port runs in portable mode: settings, SRAM, saved games and the title
logo go there. Games without an executable or files are skipped.

The output holds the user's game files: it is for the user only, never published or committed.
"""
from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ROOT = REPO.parent

GAMES = {"mslug": "Metal Slug", "mslug4": "Metal Slug 4", "mslug5": "Metal Slug 5", "mslug3": "Metal Slug 3", "mslugx": "Metal Slug X"}
# Games whose runtime decrypts the MAME set at load: their copy takes the set itself (relative to
# the given folder) when it is there, instead of the decrypted files.
MAME_SETS = {"mslug5": ["mslug5.zip", "neogeo/sp-u2.sp1", "neogeo/000-lo.lo", "neogeo/sfix.sfix"],
             "mslug4": [f"mslug4h/263-{n}" for n in ("ph1.p1", "ph2.sp2", "m1.m1", "v1.v1", "v2.v2", "c1.c1",
                                                    "c2.c2", "c3.c3", "c4.c4", "c5.c5", "c6.c6")]
                       + ["neogeo/sp-u2.sp1", "neogeo/000-lo.lo", "neogeo/sfix.sfix"]}
# Metal Slug 4: GOG's Code Mystics files and the system files the GOG copy lacks (the MAME BIOS
# set's; docs/PHASE2_HARDWARE.md), relative to the given folder.
CODEMYSTICS = {"mslug4": ["gog/p1.bin", "gog/c1.bin", "gog/s2.bin", "gog/m1.bin", "gog/v1.bin",
                          "neogeo/sp-u2.sp1", "neogeo/000-lo.lo", "neogeo/sfix.sfix"]}

README = """{title} - portable native port

Run {exe}. Settings, saved games and the battery RAM are kept in the "save" folder next to it,
so nothing is installed or written on the computer. Your game files are in the "game" folder.

Keys: Esc menu - F5/F9 save/load state - F11 fullscreen - F12 screenshot - F3 debug panel.
Player 1: arrows, Z shoot, X jump, C grenade, 1 start, 5 coin, B autofire.
Gamepads work out of the box (Guide or Back+Start opens the menu, Share takes a screenshot).
Screenshots go to the "screenshots" folder inside "save".

--

Ejecuta {exe}. Los ajustes, las partidas guardadas y la memoria de la placa se guardan en la
carpeta "save" junto a el: no se instala ni se escribe nada en la computadora. Tus archivos del
juego estan en la carpeta "game".

Teclas: Esc menu - F5/F9 guardar/cargar partida - F11 pantalla completa - F12 captura -
F3 panel de depuracion. Jugador 1: flechas, Z disparar, X saltar, C granada, 1 start,
5 moneda, B disparo automatico. Los mandos funcionan sin configurar (Guide o Back+Start abre
el menu; Share saca una captura). Las capturas van a la carpeta "screenshots" dentro de "save".

{title} is (c) SNK CORPORATION. Unofficial fan project, not affiliated with or endorsed by SNK.
"""


def copy_game(game: str, files: Path, build: Path, out: Path, mame_set: bool = False) -> int:
    exe = build / "src" / f"{game}.exe"
    dll = build / "src" / "SDL3.dll"
    if not dll.is_file():
        print(f"missing {dll}: build first", file=sys.stderr)
        return 1
    gog = [files / f for f in CODEMYSTICS.get(game, [])]
    use_gog = bool(gog) and not mame_set and all(p.is_file() for p in gog)
    mame = [files / f for f in MAME_SETS.get(game, [])]
    use_mame = not use_gog and bool(mame) and all(p.is_file() for p in mame)
    if use_gog:
        data = gog
    elif use_mame:
        data = mame
    elif game in CODEMYSTICS:
        data = []
    else:
        data = sorted(p for p in files.iterdir() if p.is_file() and p.name.startswith(f"{game}_")) if files.is_dir() else []
    if not exe.is_file() or not data:
        print(f"{GAMES[game]}: skipped ({'no ' + exe.name if not exe.is_file() else f'no {game}_* files in {files}'})")
        return 0
    dest = out / "Metal Slug" / GAMES[game]
    (dest / "game").mkdir(parents=True, exist_ok=True)
    (dest / "save").mkdir(exist_ok=True)
    shutil.copy2(exe, dest)
    shutil.copy2(dll, dest)
    for name in ("LICENSE", "THIRD_PARTY.md"):
        if (REPO / name).is_file():
            shutil.copy2(REPO / name, dest / (name + ".txt" if "." not in name else name))
    if (REPO / "licenses").is_dir():
        shutil.copytree(REPO / "licenses", dest / "licenses", dirs_exist_ok=True)
    if use_mame or use_gog:           # an earlier copy's other files would load first
        for old in (dest / "game").glob(f"{game}_*"):
            old.unlink()
        other = "gog" if use_mame else "mslug4h"
        if game == "mslug4" and (dest / "game" / other).is_dir():
            shutil.rmtree(dest / "game" / other)
    for f in data:
        target = dest / "game" / f.relative_to(files)
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or target.stat().st_size != f.stat().st_size:
            shutil.copy2(f, target)
    (dest / "README.txt").write_text(README.format(title=GAMES[game], exe=exe.name), encoding="utf-8")
    size = sum(p.stat().st_size for p in dest.rglob("*") if p.is_file())
    kind = ", GOG's files" if use_gog else ", the MAME set" if use_mame else ""
    print(f"{GAMES[game]}: {dest} ({size / 2**20:.0f} MB, {len(data)} game files{kind})")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, required=True, help="destination (a drive or a folder)")
    ap.add_argument("--build", type=Path, default=REPO / "build")
    ap.add_argument("--mslug", type=Path, default=ROOT / "roms", help="folder with the mslug_* files")
    ap.add_argument("--mslug4", type=Path, default=ROOT.parent / "MS4_Port" / "roms",
                    help="folder with gog\\ (GOG's Data\\rom files) and neogeo\\ (the system files)")
    ap.add_argument("--mslug5", type=Path, default=ROOT.parent / "MS5_Port" / "roms", help="folder with the mslug5_* files")
    ap.add_argument("--mslug3", type=Path, default=ROOT.parent / "MS3_Port" / "roms", help="folder with the mslug3_* files")
    ap.add_argument("--mslugx", type=Path, default=ROOT.parent / "roms", help="folder with the mslugx_* files")
    ap.add_argument("--only", choices=list(GAMES), help="copy only this game")
    ap.add_argument("--mame-set", action="store_true",
                    help="Metal Slug 4: copy the MAME set (mslug4h and neogeo) instead of GOG's files")
    a = ap.parse_args()
    if REPO in a.out.resolve().parents or a.out.resolve() == REPO:
        print("the output must be outside the repository", file=sys.stderr)
        return 2
    r = 0
    for game in GAMES:
        if not a.only or game == a.only:
            r |= copy_game(game, getattr(a, game), a.build, a.out, a.mame_set)
    return r


if __name__ == "__main__":
    sys.exit(main())
