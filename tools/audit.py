"""Publication audit: nothing of the game may be in the repository history or in a release.

Usage:
  python tools/audit.py history [--roms DIR]
  python tools/audit.py release ZIP [--roms DIR]

history: every file of every commit (all branches) must be text of an allowed kind (the
  .gitignore whitelist), at most 1 MB, and must not hold long runs of hex, base64 or byte-array
  literals (encoded data).
release: the zip may only hold the expected files, and its executable must have been built
  without the recompiled program.
Both: no run of varied bytes copied from the user's ROM files may appear in any file. The folders
  searched are --roms (repeatable) or, by default, PD_ROMS plus the ROM folders of the other Metal
  Slug workspaces next to this one (Metal Slug X, 3, 4 and 5: their code is built here as
  regression targets); --own searches PD_ROMS only. Every file under each folder and every member
  of its zips is read, so the GOG copy, the BIOS, the MAME set rebuilt from GOG and any region
  dumped from MAME are all searched. The search is exhaustive (RomIndex): it catches about 95 % of
  32-byte copies and practically every longer one.

Exit code 0 = clean. This script contains no ROM data; it reads the user's copy to compare.
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
TEXT_SUFFIXES = {".md", ".c", ".h", ".py", ".ps1", ".bat", ".rc", ".lua", ".java", ".csv", ".txt",
                 ".cmake", ".json", ".toml", ".cpp", ".ipp", ".manifest", ".yml", ""}
LIMIT = 1 << 20
HEX_RUN = re.compile(rb"[0-9A-Fa-f]{129,}")                 # longer than a SHA-512 in hex
BASE64_RUN = re.compile(rb"[A-Za-z0-9+/]{200,}={0,2}")
BYTE_ARRAY = re.compile(rb"(?:0x[0-9A-Fa-f]{2}\s*,\s*){64,}")
RELEASE_FILES = {"mslug.exe", "SDL3.dll", "LICENSE.txt", "README.txt", "THIRD_PARTY.md",
                 "licenses/SDL3.txt", "licenses/Musashi.txt", "licenses/ymfm.txt",
                 "licenses/chips-z80.txt", "licenses/stb_truetype.txt", "licenses/OFL-BlackOpsOne.txt",
                 "licenses/OFL-ChakraPetch.txt", "game/PUT_YOUR_GAME_HERE.txt"}
# Byte arrays allowed outside external/, one by one, each with its reason (checked by the ROM
# search like every other file):
BYTE_ARRAY_OK = {
    # The CMC42 sprite encryption's key tables (the cipher's parameters, learned from MAME as a
    # black box with synthetic patterns, the Metal Slug 3 port's docs/PHASE5_LOADER.md): no game
    # data. They let the Metal Slug 3 regression target load MAME's encrypted set; kept by the
    # user's decision in that port (Phase 6). They arrive with the import (Phase 1).
    "src/games/mslug3/cmc_tables.h",
    # Metal Slug 5's decryption key tables (NEO-PVC, PCM2, CMC50 and the M1's, learned from MAME as
    # a black box with synthetic patterns, the Metal Slug 5 port's docs/PHASE5_LOADER.md): no game
    # data. They let the Metal Slug 5 regression target load MAME's encrypted set; kept by the
    # user's decision in that port (Phase 5, 2026-10-18). They arrive with the import (Phase 1).
    "src/games/mslug5/crypt_tables.h",
    # Metal Slug 4's decryption key tables (PCM2, CMC50 and the M1's, learned from MAME as a black
    # box with synthetic patterns, the Metal Slug 4 port's docs/PHASE5_LOADER.md): no game data.
    # They let the Metal Slug 4 regression target load MAME's encrypted set; authorized by the user
    # in that port (Phase 5, 2026-10-24). They arrive with the import (Phase 1).
    "src/games/mslug4/crypt_tables.h",
}
# The other workspaces' ROM folders, relative to this workspace's parent (C:\Projecto Decompile).
OTHER_ROMS = ["roms", "MS3_Port/roms", "MS4_Port/roms", "MS5_Port/roms"]


def rom_files(roms: Path):
    """(name, bytes) of every file under the ROM folder, subfolders included (the BIOS in
    neogeo/, MAME's decrypted regions), and of every member of the zips there (the MAME set)."""
    for f in sorted(p for p in roms.rglob("*") if p.is_file()):
        if f.name.upper().startswith("MANIFEST"):
            continue
        if zipfile.is_zipfile(f):
            with zipfile.ZipFile(f) as z:
                for n in sorted(z.namelist()):
                    if not n.endswith("/"):
                        yield f"{f.name}/{n}", z.read(n)
        else:
            yield str(f.relative_to(roms)), f.read_bytes()


class RomIndex:
    """Every 16-byte run of the user's ROM files that starts at an 8-aligned offset, as a 64-bit
    hash of its two 8-byte blocks (sorted keys). A file is flagged when two of them match 8 bytes
    apart, which any copy of 31 or more bytes contains. Runs with fewer than 8 distinct byte
    values, monotonic runs and arithmetic progressions (padding, fills, offset tables) are left
    out: they also occur in unrelated code."""

    MIX = 0x9E3779B97F4A7C15

    def __init__(self, folders: list[Path]):
        import numpy as np
        self.np = np
        self.keys = None
        parts = []
        for _, data in (r for roms in folders if roms.is_dir() for r in rom_files(roms)):
            d = np.frombuffer(data, np.uint8)
            blocks = d[:len(d) // 8 * 8].reshape(-1, 8)
            if len(blocks) < 2:
                continue
            pairs = np.concatenate([blocks[:-1], blocks[1:]], axis=1)
            s = np.sort(pairs, axis=1)
            distinct = 1 + (s[:, 1:] != s[:, :-1]).sum(axis=1)
            # Simple sequences (ascending / descending like 00 00 01 01 02 02..., or few distinct
            # steps) also occur in ordinary code tables, such as DEFLATE's: left out too.
            steps = np.diff(pairs.astype(np.int16), axis=1)
            monotonic = (steps >= 0).all(axis=1) | (steps <= 0).all(axis=1)
            ss = np.sort(steps, axis=1)
            step_kinds = 1 + (ss[:, 1:] != ss[:, :-1]).sum(axis=1)
            keep = (distinct >= 8) & ~monotonic & (step_kinds >= 4)
            # Arithmetic progressions of 16- or 32-bit values (offset tables such as 0000 0004
            # 0008 000C...), in either byte order.
            p64 = pairs.astype(np.int64)
            for width in (2, 4):
                for order in (range(width), range(width - 1, -1, -1)):
                    v = sum(p64[:, k::width] << (8 * i) for i, k in enumerate(order))
                    dv = np.sort(np.diff(v, axis=1), axis=1)
                    kinds = 1 + (dv[:, 1:] != dv[:, :-1]).sum(axis=1)
                    keep &= kinds > 2 if width == 2 else kinds > 1
            w = np.ascontiguousarray(blocks).view("<u8").ravel()
            parts.append(np.unique(self.mix(w[:-1], w[1:])[keep]))   # unique per file: less memory
        self.keys = np.unique(np.concatenate(parts)) if parts else None

    def fmix(self, x):
        """MurmurHash3's 64-bit finalizer: every input bit affects every output bit."""
        np = self.np
        x = x ^ (x >> np.uint64(33))
        x = x * np.uint64(0xFF51AFD7ED558CCD)
        x = x ^ (x >> np.uint64(33))
        x = x * np.uint64(0xC4CEB9FE1A85EC53)
        return x ^ (x >> np.uint64(33))

    def mix(self, a, b):
        np = self.np
        with np.errstate(over="ignore"):
            return self.fmix(self.fmix(a) * np.uint64(self.MIX) + (b ^ np.uint64(0x632BE59BD9B4E019)))

    def __bool__(self) -> bool:
        return self.keys is not None and len(self.keys) > 0

    def find(self, data: bytes) -> int:
        """Offset of the first 16-byte run equal to an indexed ROM run, or -1."""
        np = self.np
        if not self or len(data) < 16:
            return -1
        a = np.frombuffer(data, np.uint8)
        w = np.ascontiguousarray(np.lib.stride_tricks.sliding_window_view(a, 8)).view("<u8").ravel()
        h = self.mix(w[:-8], w[8:])
        i = np.searchsorted(self.keys, h)
        i[i >= len(self.keys)] = 0
        hit = self.keys[i] == h
        # Two overlapping matches (24 bytes): single 16-byte matches do happen by chance in
        # structured tables of unrelated programs (seen in MAME's executable).
        where = np.flatnonzero(hit[:-8] & hit[8:])
        return int(where[0]) if len(where) else -1


def find_pieces(data: bytes, index: RomIndex, where: str, problems: list[str]) -> None:
    o = index.find(data)
    if o >= 0:
        problems.append(f"{where}: contains bytes copied from the ROMs (offset {o})")


def git(*args: str) -> bytes:
    return subprocess.run(["git", *args], cwd=REPO, capture_output=True, check=True).stdout


def audit_history(pieces) -> list[str]:
    problems = []
    blobs = {}
    for line in git("rev-list", "--objects", "--all").decode("utf-8", "replace").splitlines():
        sha, _, path = line.partition(" ")
        if path:
            blobs.setdefault(sha, path)
    checked = 0
    proc = subprocess.run(["git", "cat-file", "--batch-check=%(objectname) %(objecttype) %(objectsize)"],
                          cwd=REPO, input="\n".join(blobs).encode(), capture_output=True, check=True)
    for row in proc.stdout.decode().splitlines():
        sha, kind, size = row.split()
        if kind != "blob":
            continue
        path = blobs[sha]
        checked += 1
        suffix = Path(path).suffix.lower()
        if suffix not in TEXT_SUFFIXES:
            problems.append(f"{path}: file type not in the whitelist")
        if int(size) > LIMIT:
            problems.append(f"{path}: {int(size)} bytes (over 1 MB)")
        data = git("cat-file", "blob", sha)
        if b"\0" in data:
            problems.append(f"{path}: binary content (NUL bytes)")
        for rx, what in ((HEX_RUN, "hex"), (BASE64_RUN, "base64"), (BYTE_ARRAY, "byte array")):
            if rx is BYTE_ARRAY and (path.startswith("external/") or path in BYTE_ARRAY_OK):
                continue                # upstream libraries' own tables; BYTE_ARRAY_OK
            m = rx.search(data)
            if m:
                problems.append(f"{path}: long {what} run at offset {m.start()}")
        find_pieces(data, pieces, path, problems)
    print(f"history: {checked} files (every version of every file), ROM search {'on' if pieces else 'skipped'}")
    return problems


def audit_release(zip_path: Path, pieces) -> list[str]:
    problems = []
    with zipfile.ZipFile(zip_path) as z:
        names = {n.split("/", 1)[1] if "/" in n else n for n in z.namelist() if not n.endswith("/")}
        top = {n.split("/", 1)[0] for n in z.namelist()}
        if len(top) != 1:
            problems.append(f"expected one top folder, found {sorted(top)}")
        for n in sorted(names - RELEASE_FILES):
            problems.append(f"unexpected file in the zip: {n}")
        for n in sorted(RELEASE_FILES - names):
            problems.append(f"missing from the zip: {n}")
        for n in z.namelist():
            if not n.endswith("/"):
                data = z.read(n)
                find_pieces(data, pieces, n, problems)
                if n.endswith("mslug.exe") and len(data) > 4 * LIMIT:
                    problems.append(f"{n}: {len(data)} bytes, too big for the interpreter-only build")
    print(f"release: {len(names)} files, ROM search {'on' if pieces else 'skipped'}")
    return problems


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("what", choices=["history", "release"])
    ap.add_argument("zip", nargs="?", type=Path)
    ap.add_argument("--roms", type=Path, action="append", help="ROM folder to search (repeatable)")
    ap.add_argument("--own", action="store_true", help="search PD_ROMS only, not the other workspaces")
    a = ap.parse_args()
    folders = a.roms or []
    if not folders:
        if os.environ.get("PD_ROMS"):
            folders.append(Path(os.environ["PD_ROMS"]))
        if not a.own:
            parent = REPO.parents[1]
            folders += [parent / r for r in OTHER_ROMS]
    folders = [f for f in folders if f.is_dir()]
    for f in folders:
        print(f"ROM folder: {f}")
    pieces = RomIndex(folders)
    if not pieces:
        print("warning: no ROM folder (--roms / PD_ROMS): the byte search is skipped")
    if a.what == "history":
        problems = audit_history(pieces)
    else:
        if not a.zip:
            ap.error("release needs the zip")
        problems = audit_release(a.zip, pieces)
    for p in problems:
        print("PROBLEM:", p)
    print("clean" if not problems else f"{len(problems)} problem(s)")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
