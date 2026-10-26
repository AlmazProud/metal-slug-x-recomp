"""Simulated Steam install for the game search (src/port/find_game.c): a Steam folder with the
game in its own library and a second library listed in steamapps/libraryfolders.vdf. The
executable (PD_FIND_PLACES=1, PD_STEAM_ROOT=<simulated Steam>) must list both install folders.
Only empty marker files are written (the 68000 program's name), never game data.

Usage: python steam_layout.py EXE GAME_ID OUT_FOLDER
"""
import os
import subprocess
import sys
from pathlib import Path


def main() -> int:
    exe, game, out = str(Path(sys.argv[1]).resolve()), sys.argv[2], Path(sys.argv[3]).resolve()
    steam, library = out / "Steam", out / "SteamLibrary"
    installs = [steam / "steamapps" / "common" / "Game A", library / "steamapps" / "common" / "Game B"]
    for d in installs:
        (d / "resources" / "game").mkdir(parents=True, exist_ok=True)
        (d / "resources" / "game" / f"{game}_game_m68k").write_bytes(b"")
    escaped = str(library).replace("\\", "\\\\")
    (steam / "steamapps" / "libraryfolders.vdf").write_text(
        '"libraryfolders"\n{\n\t"0"\n\t{\n\t\t"path"\t\t"' + str(steam).replace("\\", "\\\\") +
        '"\n\t}\n\t"1"\n\t{\n\t\t"path"\t\t"' + escaped + '"\n\t}\n}\n', encoding="utf-8")
    env = dict(os.environ, PD_FIND_PLACES="1", PD_STEAM_ROOT=str(steam))
    r = subprocess.run([exe, "--headless", "--frames", "1"], env=env, capture_output=True, text=True)
    places = [line[len("place: "):] for line in r.stdout.splitlines() if line.startswith("place: ")]
    print("\n".join(places))
    missing = [str(d) for d in installs if str(d) not in places]
    print("OK" if not missing else f"MISSING: {missing}")
    return 0 if not missing else 1


if __name__ == "__main__":
    sys.exit(main())
