"""Pre-commit hook: enforces rule 1 (the repo never contains ROMs or extracted data).

Rejects the commit if any staged file:
  - is binary (contains NUL bytes): the repo is text only;
  - is larger than 1 MB;
  - has the same SHA256 as any file under the user's ROM folder or any member of a zip there
    (the GOG copy in roms/gog, the BIOS in roms/neogeo, the MAME set rebuilt from GOG, any region
    dumped from MAME).

It also rejects it when a source file (code or docs) in the working tree is ignored by the
whitelist .gitignore, outside the ignored folders (build*, roms...): such a file would silently
stay out of the commit (e.g. src/games/mslug3_profile.c matched the ROM rule mslug3_*).
"""
from __future__ import annotations

import hashlib
import os
import subprocess
import sys
import zipfile
from pathlib import Path

LIMIT = 1 << 20
SOURCE_SUFFIXES = {".c", ".h", ".cpp", ".ipp", ".py", ".lua", ".java", ".md", ".txt", ".cmake",
                   ".json", ".toml", ".ps1", ".bat", ".rc", ".manifest", ".yml"}


def git(*args: str) -> bytes:
    return subprocess.run(["git", *args], capture_output=True, check=True).stdout


def rom_hashes(folder: Path) -> dict[str, str]:
    h = {}
    if folder.is_dir():
        for f in folder.rglob("*"):
            if f.is_file():
                name = str(f.relative_to(folder))
                h[hashlib.sha256(f.read_bytes()).hexdigest()] = name
                if zipfile.is_zipfile(f):
                    with zipfile.ZipFile(f) as z:
                        for n in z.namelist():
                            if not n.endswith("/"):
                                h[hashlib.sha256(z.read(n)).hexdigest()] = f"{name}/{n}"
    return h


def main() -> int:
    repo_root = Path(git("rev-parse", "--show-toplevel").decode().strip())
    roms = Path(os.environ.get("PD_ROMS") or repo_root.parent / "roms")
    forbidden = rom_hashes(roms)

    names = [n for n in git("diff", "--cached", "--name-only", "--diff-filter=ACMR", "-z").decode().split("\0") if n]
    problems = []
    for n in names:
        data = git("show", f":{n}")
        if b"\0" in data:
            problems.append(f"{n}: is binary")
        if len(data) > LIMIT:
            problems.append(f"{n}: is {len(data)} bytes (limit {LIMIT})")
        match = forbidden.get(hashlib.sha256(data).hexdigest())
        if match:
            problems.append(f"{n}: identical to roms/{match}")

    # Ignored files only (ignored folders are listed as "folder/" and skipped).
    for n in git("ls-files", "--others", "--ignored", "--exclude-standard", "--directory", "-z").decode().split("\0"):
        if n and not n.endswith("/") and Path(n).suffix.lower() in SOURCE_SUFFIXES:
            problems.append(f"{n}: source file ignored by .gitignore (rename it, or whitelist it on purpose)")

    if problems:
        print("pre-commit: REJECTED (rule 1: the repo never contains ROMs or extracted data;"
              " no source file may be left out by the .gitignore)")
        for p in problems:
            print("   " + p)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
