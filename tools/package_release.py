"""Builds the release zip from the distributable build (interpreter only, no game code).

Usage:
  python tools/package_release.py [--build repo/build-dist] [--out <root>/release]

Refuses a build that contains the recompiled program (PD_HAVE_PROGRAM_mslugx in its CMake
cache). The zip holds one folder, metal-slug-x-recomp-vX.Y.Z-win64/, with the executable,
SDL3.dll, the licenses, a short README.txt and an empty game/ folder. Then it runs
tools/audit.py on it.
"""
from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ROOT = REPO.parent
# the shared tools, as env.ps1 finds them: PD_TOOLS, <root>\tools, or the parent's tools\ folder
TOOLS = Path(os.environ["PD_TOOLS"]) if os.environ.get("PD_TOOLS") else \
    ROOT / "tools" if (ROOT / "tools").is_dir() else ROOT.parent / "tools"

README = """metal-slug-x-recomp {version}: native port of Metal Slug X for Windows
https://github.com/AlmazProud/metal-slug-x-recomp

This package contains no part of the game. You need your own copy, either:
- the GOG or Steam release: if it is installed, the port finds it by itself (or copy its install
  folder into the "game" folder here); or
- an arcade board dump (MAME's mslugx set): mslugx.zip and neogeo.zip (or their files) in the
  "game" folder.

Run mslugx.exe. If the game is not found, drop its folder or zip on the window, or press ENTER
to choose a folder.

Keys: Esc menu - F5/F9 save/load state - F11 fullscreen - F12 screenshot - F3 debug panel.
Player 1: arrows, Z shoot, X jump, C grenade, 1 start, 5 coin, B autofire.
Gamepads work out of the box (Guide or Back+Start opens the menu, Share takes a screenshot).

Settings, saves, records and screenshots go to %APPDATA%\\ProjectoDecompile\\mslugx. To keep
them next to the executable instead (portable, e.g. on a USB drive), create a folder named
"save" here.

Metal Slug X is (c) SNK CORPORATION. This is an unofficial fan project, not affiliated with
or endorsed by SNK. The port's code is MIT licensed (LICENSE.txt); third-party licenses are in
the "licenses" folder and THIRD_PARTY.md.
"""

GAME_README = """Put your copy of Metal Slug X here (any of these):

  - the install folder of the GOG or Steam release (or its resources\\game folder)
  - mslugx.zip and neogeo.zip from your MAME set (or their files)

The files are recognized by their contents, so names and subfolders do not matter.
"""


def stb_license() -> bytes:
    """stb_truetype's license, the block at the end of its header."""
    t = (REPO / "external" / "stb" / "stb_truetype.h").read_text(encoding="utf-8")
    i = t.rindex("This software is available under 2 licenses")
    return t[i:t.index("*/", i)].strip().replace("\n", "\r\n").encode() + b"\r\n"


def version() -> str:
    t = (REPO / "src" / "port" / "version.h").read_text(encoding="utf-8")
    return re.search(r'#define PORT_VERSION "([^"]+)"', t).group(1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--build", type=Path, default=REPO / "build-dist")
    ap.add_argument("--out", type=Path, default=ROOT / "release")
    a = ap.parse_args()

    cache = (a.build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace")
    if "PD_HAVE_PROGRAM_mslugx:INTERNAL=OFF" not in cache:
        sys.exit(f"{a.build} has the recompiled program (code derived from the game): build with "
                 "-DPD_RECOMPILED=OFF")
    exe = a.build / "src" / "mslugx.exe"
    dll = a.build / "src" / "SDL3.dll"
    out = subprocess.run([str(exe), "--version"], capture_output=True, text=True).stdout.strip()
    v = version()
    if not out.endswith(v):
        sys.exit(f"the executable says '{out}', version.h says {v}: rebuild first")

    sdl_license = TOOLS / "sdl3" / "LICENSE.txt"
    files = {
        "mslugx.exe": exe.read_bytes(),
        "SDL3.dll": dll.read_bytes(),
        "LICENSE.txt": (REPO / "LICENSE").read_bytes(),
        "README.txt": README.format(version=v).replace("\n", "\r\n").encode(),
        "THIRD_PARTY.md": (REPO / "THIRD_PARTY.md").read_bytes(),
        "licenses/SDL3.txt": sdl_license.read_bytes(),
        "licenses/Musashi.txt": (REPO / "external" / "musashi" / "readme.txt").read_bytes(),
        "licenses/ymfm.txt": (REPO / "external" / "ymfm" / "LICENSE").read_bytes(),
        "licenses/chips-z80.txt": (REPO / "external" / "chips" / "LICENSE").read_bytes(),
        "licenses/stb_truetype.txt": stb_license(),
        "licenses/OFL-BlackOpsOne.txt": (REPO / "external" / "fonts" / "OFL-BlackOpsOne.txt").read_bytes(),
        "licenses/OFL-ChakraPetch.txt": (REPO / "external" / "fonts" / "OFL-ChakraPetch.txt").read_bytes(),
        "game/PUT_YOUR_GAME_HERE.txt": GAME_README.replace("\n", "\r\n").encode(),
    }
    a.out.mkdir(parents=True, exist_ok=True)
    top = f"metal-slug-x-recomp-v{v}-win64"
    zip_path = a.out / f"{top}.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name, data in files.items():
            z.writestr(f"{top}/{name}", data)
    print(f"{zip_path}  ({zip_path.stat().st_size} bytes)")
    return subprocess.run([sys.executable, str(REPO / "tools" / "audit.py"), "release", str(zip_path)]).returncode


if __name__ == "__main__":
    sys.exit(main())
