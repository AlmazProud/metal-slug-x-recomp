"""Generates src/games/<game>/sounds.c: the list of a game's music, effects and voices for the
sound menu and rumble (Phase 6B).

Input: the catalogs produced by the port, one per sound driver mode:
  mslugx --headless --frames 1300 --sound-catalog cat_pMM.tsv   with PD_CATALOG_PREFIX=MM
(MM = 00 music, 10 effects, 11 voices; frame 1300 is right after the game resets the driver).
Only command numbers, durations and names are stored: no audio data.

Valid ranges (anything outside is the driver reading past its tables):
  music 20-31, effects 20-FF, voices 20-6E.
Names: only the ones confirmed with screenshots of the recorded playthrough
(docs/PHASE6_REMASTER.md); the rest are identified by number.

Metal Slug 3 (MS3_NAMES, MS3_RANGES; docs/PHASE4_PROFILE.md): the same driver modes, music 20-3F;
names confirmed by the moment they play in the recorded runs, or by matching Metal Slug X's named
sound (same mode, correlation >= 0.99 of the catalog audio: the same sample), or by the game's
weapon records (announcer voice).

Metal Slug 5 (MS5_MODES; docs/PHASE4_PROFILE.md): another driver. Every sound is a mode command
and a command; music is ADPCM-B (modes 07 and 19), effects and voices ADPCM-A (1A, 18). The ranges
are the commands that play something of their own (the others repeat a neighbour or play nothing);
names only from the game's weapon records (announcer voice) until confirmed otherwise.

Metal Slug (MS1_RANGES, MS1_NAMES; docs/PHASE4_PROFILE.md): Metal Slug X's driver modes, music
20-2C, voices 21-40 (beyond them the driver reads past its tables); the catalog is taken at frame
1060 (the silent black screen before the game's first image). Names by the moment they play in the
user's recorded game, the announcer voices by the weapon picked up.

Usage: python sound_catalog.py CATALOG_DIR OUTPUT.c [mslugx|mslug3|mslug5|mslug4|mslug]
"""
from __future__ import annotations

import sys
from pathlib import Path

RANGES = {"00": (0x20, 0x31), "10": (0x20, 0xFF), "11": (0x20, 0x6E)}
NAMES = {
    ("00", 0x20): "Mission complete (fanfare)",
    ("00", 0x21): "Mission 1",
    ("00", 0x22): "Mission 2",
    ("00", 0x2C): "Tank battle",
    ("00", 0x2E): "How to play",
    ("00", 0x30): "Soldier select",
    ("10", 0x22): "Big explosion",
    ("10", 0x25): "Explosion",
    ("10", 0x27): "Grenade explosion",
    ("10", 0x48): "Rope cut",
    ("10", 0x84): "Slug cannon",
    ("10", 0x8C): "Slug vulcan",
    ("10", 0xD5): "GO! sign",
    ("10", 0xE8): "Helicopter",
    ("10", 0xF2): "Armor hit",
    ("10", 0xF7): "Pistol",
    ("10", 0xF8): "Laser",
    ("11", 0x21): "Coin",
    ("11", 0x23): "Male scream",
    ("11", 0x24): "Okay!",
    ("11", 0x25): "Female scream",
    ("11", 0x2C): "Flame Shot!",
    ("11", 0x2D): "Heavy Machine Gun!",
    ("11", 0x2E): "Rocket Launcher!",
    ("11", 0x2F): "Shotgun!",
    ("11", 0x30): "Laser Gun!",
    ("11", 0x31): "Mission 1... start!",
    ("11", 0x32): "Mission 2... start!",
    ("11", 0x33): "Mission 3... start!",
    ("11", 0x34): "Mission 4... start!",
    ("11", 0x35): "Mission 5... start!",
    ("11", 0x36): "Final mission... start!",
    ("11", 0x37): "Mission complete!",
    ("11", 0x3A): "Thank you!",
    ("11", 0x3C): "Come on, boy!",
    ("11", 0x3D): "Go home to mommy!",
    ("11", 0x40): "See you in hell!",
    ("11", 0x46): "Burp",
    ("11", 0x49): "Marco (soldier select)",
    ("11", 0x4A): "Tarma (soldier select)",
    ("11", 0x4B): "Fio (soldier select)",
    ("11", 0x4C): "Eri (soldier select)",
    ("11", 0x50): "Great!",
    ("11", 0x51): "Mission all over!",
    ("11", 0x53): "Wow, big!",
    ("11", 0x54): "Armor Piercer!",
    ("11", 0x55): "Stone!",
    ("11", 0x56): "Fire Bomb!",
    ("11", 0x57): "Grenade!",
    ("11", 0x58): "Enemy Chaser!",
    ("11", 0x59): "Super Grenade!",
    ("11", 0x5B): "Drop Shot!",
    ("11", 0x5C): "Mobile Satellite!",
    ("11", 0x5D): "Thunder Cloud!",
}
MS3_RANGES = {"00": (0x20, 0x3F), "10": (0x20, 0xFF), "11": (0x20, 0xFF)}
MS3_NAMES = {
    # music: by the moment it plays (the user's recording and the scripted missions)
    ("00", 0x20): "Mission complete (fanfare)",
    ("00", 0x21): "How to play",
    ("00", 0x22): "Game over",
    ("00", 0x23): "Soldier select",
    ("00", 0x24): "Boss battle",
    ("00", 0x25): "Mission 1",
    ("00", 0x28): "Mission 2",
    ("00", 0x2B): "Mission 4",
    ("00", 0x2D): "Mission 5",
    ("00", 0x35): "Mission 3",
    # effects: the same sample as Metal Slug X's
    ("10", 0x22): "Big explosion",
    ("10", 0x25): "Explosion",
    ("10", 0x2A): "Grenade explosion",
    ("10", 0x8C): "Slug vulcan",
    ("10", 0xF2): "Armor hit",
    ("10", 0xF7): "Pistol",
    # voices: Metal Slug X's sample, the weapon records (0x21C2E) or the mission start
    ("11", 0x24): "Okay!",
    ("11", 0x25): "Female scream",
    ("11", 0x2C): "Flame Shot!",
    ("11", 0x2D): "Heavy Machine Gun!",
    ("11", 0x2E): "Rocket Launcher!",
    ("11", 0x2F): "Shotgun!",
    ("11", 0x30): "Laser Gun!",
    ("11", 0x31): "Mission 1... start!",
    ("11", 0x32): "Mission 2... start!",
    ("11", 0x33): "Mission 3... start!",
    ("11", 0x34): "Mission 4... start!",
    ("11", 0x35): "Mission 5... start!",
    ("11", 0x37): "Mission complete!",
    ("11", 0x3A): "Thank you!",
    ("11", 0x3C): "Come on, boy!",
    ("11", 0x3D): "Go home to mommy!",
    ("11", 0x54): "Armor Piercer!",
    ("11", 0x55): "Stone!",
    ("11", 0x56): "Fire Bomb!",
    ("11", 0x57): "Grenade!",
    ("11", 0x58): "Enemy Chaser!",
    ("11", 0x59): "Super Grenade!",
    ("11", 0x5A): "Iron Lizard!",
    ("11", 0x5B): "Drop Shot!",
    ("11", 0x5C): "Mobile Satellite!",
    ("11", 0x5D): "Thunder Cloud!",
}
NAME_PREFIX = {"00": "Track", "10": "Effect", "11": "Voice"}

# Metal Slug 5: mode -> (first, last, name prefix).
MS5_MODES = {"07": (0x21, 0x50, "Track"), "19": (0xD0, 0xE0, "Track"),
             "1A": (0x20, 0xDF, "Effect"), "18": (0x19, 0xE5, "Voice")}
MS5_NAMES = {
    # voices: the weapon records (0x1C298, announcer voice)
    ("18", 0xC3): "Flame Shot!",
    ("18", 0xC4): "Heavy Machine Gun!",
    ("18", 0xC5): "Rocket Launcher!",
    ("18", 0xC6): "Shotgun!",
    ("18", 0xC7): "Laser Gun!",
    ("18", 0xD6): "Enemy Chaser!",
    ("18", 0xD7): "Super Grenade!",
    ("18", 0xD8): "Iron Lizard!",
    ("18", 0xD9): "Drop Shot!",
}

# Metal Slug 4 (docs/PHASE4_PROFILE.md): Metal Slug 5's driver. Catalogs taken at frame 700 (the
# intro's black screen: the driver is started and silent; at 1300 the intro music plays). Music in
# mode 08 (21-33; the game plays its music there, 07 gives the same catalog), effects in 1A
# (20-79), voices in 18 (1F-F0); mode 19 plays one sound for every command and is left out.
MS1_RANGES = {"00": (0x20, 0x2C), "10": (0x20, 0xFF), "11": (0x21, 0x40)}
MS1_NAMES = {
    ("00", 0x20): "Mission complete (fanfare)",
    ("00", 0x21): "Mission 1",
    ("00", 0x22): "Mission 2",
    ("00", 0x28): "Mission 1 boss",
    ("00", 0x2B): "How to play",
    ("10", 0xF7): "Pistol",
    ("11", 0x2C): "Flame Shot (announcer)",
    ("11", 0x2D): "Heavy Machine Gun (announcer)",
    ("11", 0x31): "Mission start",
}
MS4_MODES = {"08": (0x21, 0x33, "Track"), "1A": (0x20, 0x79, "Effect"), "18": (0x1F, 0xF0, "Voice")}
MS4_NAMES = {
    # music: the moment the game plays it in the scripted run and the user's game
    ("08", 0x32): "Intro (attract)",
    ("08", 0x24): "How to play",
    ("08", 0x27): "Soldier select",
    ("08", 0x2A): "Mission 1",
    ("08", 0x23): "Mission 1 boss",
    ("08", 0x25): "Mission complete",
    ("08", 0x22): "Mission 2",
    # the weapon records (0xC522E, announcer voice)
    ("18", 0x58): "Shotgun!",
    ("18", 0x57): "Rocket Launcher!",
    ("1A", 0x43): "Flame Shot!",
    ("18", 0x52): "Heavy Machine Gun!",
    ("18", 0x53): "Laser Gun!",
    ("18", 0x77): "Enemy Chaser!",
    ("18", 0x78): "Super Grenade!",
    ("18", 0x79): "Iron Lizard!",
    ("18", 0x7A): "Drop Shot!",
}


def read_catalog(path: Path) -> dict[int, tuple[float, int]]:
    data = {}
    for line in path.read_text().splitlines():
        if line.startswith("#"):
            continue
        c = line.split("\t")
        data[int(c[0], 16)] = (float(c[1]), int(c[2]))
    return data


def main() -> int:
    folder, output = Path(sys.argv[1]), Path(sys.argv[2])
    game = sys.argv[3] if len(sys.argv) > 3 else "mslugx"     # symbol prefix: MSLUGX_SOUNDS
    title = {"mslugx": "Metal Slug X", "mslug3": "Metal Slug 3", "mslug5": "Metal Slug 5",
             "mslug4": "Metal Slug 4", "mslug": "Metal Slug"}.get(game, game)
    sym = game.upper()
    rows = []
    if game == "mslug5":
        table = MS5_MODES
    elif game == "mslug4":
        table = MS4_MODES
    else:
        ranges = MS3_RANGES if game == "mslug3" else MS1_RANGES if game == "mslug" else RANGES
        table = {m: (*ranges[m], NAME_PREFIX[m]) for m in ("00", "10", "11")}
    for mode, (lo, hi, prefix) in table.items():
        cat = read_catalog(folder / f"cat_p{mode}.tsv")
        for cmd in range(lo, hi + 1):
            secs, looping = cat.get(cmd, (0.0, 0))
            if secs <= 0:
                continue
            names = {"mslugx": NAMES, "mslug3": MS3_NAMES, "mslug4": MS4_NAMES,
                     "mslug": MS1_NAMES}.get(game, MS5_NAMES)
            name = names.get((mode, cmd), f"{prefix} {cmd:02X}")
            tenths = 0xFFFF if looping else min(int(round(secs * 10)), 0xFFFE)
            rows.append(f'    {{0x{mode}, 0x{cmd:02X}, {tenths}, "{name}"}},')
    text = (
        "/* Generated by tools/sound_catalog.py: do not edit by hand.\n"
        f" * {title} sounds: driver mode, command,\n"
        " * duration in tenths of a second (0xFFFF = looping) and name. */\n"
        '#include "profile.h"\n\n'
        f"const Sound {sym}_SOUNDS[] = {{\n" + "\n".join(rows) + "\n};\n"
        f"const int {sym}_SOUND_COUNT = (int)(sizeof {sym}_SOUNDS / sizeof {sym}_SOUNDS[0]);\n"
    )
    output.write_text(text, encoding="utf-8")
    print(f"{len(rows)} sounds -> {output}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
