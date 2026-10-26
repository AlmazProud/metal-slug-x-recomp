"""Test fixture: a folder with only a MAME set, linked (or copied) from the user's ROM folder.

Usage: python tests/loader/link_set.py ROMS DEST FILE [FILE...]
Each FILE (relative to ROMS, e.g. mslug5.zip or neogeo/sp-u2.sp1) ends up at DEST/FILE, as a hard
link when the file system allows it. A FILE ending in "!" (e.g. mslug5.zip!) is a zip whose
top-level members are extracted to DEST/<its name without .zip>/ instead (the loose set; the
clones' folders inside a merged set are left out). DEST is emptied first. Nothing leaves the
build folder.
"""
import os
import shutil
import sys
import zipfile
from pathlib import Path


def main() -> int:
    roms, dest, files = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3:]
    shutil.rmtree(dest, ignore_errors=True)
    for f in files:
        if f.endswith("!"):
            src = roms / f[:-1]
            out = dest / src.stem
            out.mkdir(parents=True, exist_ok=True)
            with zipfile.ZipFile(src) as z:
                for m in z.infolist():
                    if "/" not in m.filename and not m.is_dir():
                        (out / m.filename).write_bytes(z.read(m))
            continue
        src, out = roms / f, dest / f
        out.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(src, out)
        except OSError:
            shutil.copyfile(src, out)
    print(f"{len(files)} files in {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
