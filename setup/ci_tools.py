"""Downloads some of the pinned tools of setup/versions.json (same URLs, hashes checked) into a
folder: what continuous integration needs to build the distributable executable without the
full installer (setup.ps1, Windows PowerShell) and without the game.

Usage: python setup/ci_tools.py DEST NAME [NAME...]     e.g. cmake ninja llvm-mingw sdl3
Each tool ends up in DEST/NAME (the archive's single top folder, if any, is removed).
"""
from __future__ import annotations

import hashlib
import json
import shutil
import sys
import urllib.request
import zipfile
from pathlib import Path

SETUP = Path(__file__).resolve().parent


def main() -> int:
    dest = Path(sys.argv[1])
    wanted = sys.argv[2:]
    cfg = json.loads((SETUP / "versions.json").read_text(encoding="utf-8"))
    tools = {t["name"]: t for t in cfg["tools"]}
    dest.mkdir(parents=True, exist_ok=True)
    for name in wanted:
        t = tools[name]
        d = t["downloads"][0]
        archive = dest / d["file"]
        print(f"{name} {t['version']}: {d['url']}", flush=True)
        with urllib.request.urlopen(d["url"]) as r, open(archive, "wb") as f:
            shutil.copyfileobj(r, f)
        h = hashlib.new(d["algorithm"].lower(), archive.read_bytes()).hexdigest()
        if h != d["hash"].lower():
            sys.exit(f"{name}: {d['algorithm']} {h} does not match versions.json")
        tmp = dest / f"_tmp_{name}"
        shutil.rmtree(tmp, ignore_errors=True)
        with zipfile.ZipFile(archive) as z:
            z.extractall(tmp)
        items = list(tmp.iterdir())
        src = items[0] if len(items) == 1 and items[0].is_dir() else tmp
        target = dest / name
        shutil.rmtree(target, ignore_errors=True)
        shutil.move(str(src), str(target))
        shutil.rmtree(tmp, ignore_errors=True)
        archive.unlink()
        if not (target / t["check"].replace("\\", "/")).exists():
            sys.exit(f"{name}: {t['check']} missing after extraction")
    return 0


if __name__ == "__main__":
    sys.exit(main())
