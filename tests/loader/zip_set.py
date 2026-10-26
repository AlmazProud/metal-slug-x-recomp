"""Zips a MAME set folder (<set>/<game>/*, <set>/neogeo/*) into <out>/<game>.zip and
<out>/neogeo.zip (deflate), the way MAME users keep them: test input for the port's zip reader.
The zips are written to the build directory and never committed.

Usage: python zip_set.py SET_FOLDER OUT_FOLDER [GAME]      (GAME: mslugx by default)
"""
import sys
import zipfile
from pathlib import Path


def main() -> int:
    src, out = Path(sys.argv[1]), Path(sys.argv[2])
    game = sys.argv[3] if len(sys.argv) > 3 else "mslugx"
    out.mkdir(parents=True, exist_ok=True)
    for name in (game, "neogeo"):
        with zipfile.ZipFile(out / f"{name}.zip", "w", zipfile.ZIP_DEFLATED) as z:
            for f in sorted((src / name).iterdir()):
                z.write(f, f.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
