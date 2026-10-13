# Phase 10: publishing on GitHub

**State:** closed, pending approval.

**Repository:** https://github.com/AlmazProud/metal-slug-x-recomp
- **License:** MIT, for the port's code only. The game belongs to SNK, as `LICENSE` and the
  README state.
- **History:** kept whole.

## What was added

- **`LICENSE`:** MIT, plus a note that the game is SNK's and that third-party code keeps its
  own licenses.
- **`README.md`:**
  - what the port is;
  - legal notice;
  - quick start with GOG, Steam or a MAME set;
  - features and controls;
  - how it works and how it was verified;
  - building from source and the repository layout.
- **Version:** `src/port/version.h` sets 1.0.0. It is shown by `--version`, the F3 panel and the
  "game not found" screen, and in the executable's properties (`mslugx.rc`).
- **`tools/audit.py`:**
  - `history`: every version of every file of every commit must be whitelisted text of at most
    1 MB, without long hex, base64 or byte-array runs. Upstream opcode tables in `external/`
    are exempt from the byte-array rule.
  - `release ZIP`: only the expected files are allowed, and the executable must be small.
  - **Both:** a search for runs copied from the user's ROMs (`RomIndex`), described below.
- **`tools/package_release.py`:** builds `release/metal-slug-x-recomp-vX.Y.Z-win64.zip` from
  the interpreter-only build (it refuses a build with `PD_HAVE_PROGRAM`) and audits it. The zip
  holds:
  - `mslugx.exe` and `SDL3.dll`;
  - `LICENSE.txt` and `THIRD_PARTY.md`;
  - `licenses/` (SDL3, Musashi, ymfm, chips);
  - `README.txt`;
  - an empty `game/` folder with instructions.
- **GitHub Actions** (`.github/workflows/build.yml`): builds the distributable executable on
  every push and uploads it as an artifact.
  - The toolchain comes from `setup/versions.json` through `setup/ci_tools.py` (same URLs,
    hashes checked).
  - Tests are not run: they need the game.
  - Simulated locally in a fresh clone without ROMs or the environment: it builds.
- **Setup:** portable GitHub CLI 2.102.0 in `tools\gh`, hash-checked. Its settings live in
  `tools\_cache\gh`; the token is kept in Windows' credential store.

## The ROM search (`RomIndex`)

- **Index:** every 16-byte run of the ROMs at an 8-aligned offset, hashed with MurmurHash3's
  finalizer.
- **What it flags:** a file is flagged when two of these runs match 8 bytes apart. Any copy of
  31 or more bytes contains such a pair.
- **What it leaves out:** low-variety runs, monotonic runs and 16- or 32-bit arithmetic
  progressions.
- **False alarms it now avoids:**
  - DEFLATE's length and distance tables in `unzip.c` (`00 00 01 01 02 02...`);
  - an offset table in `SDL3.dll` (`0000 0004 0008...`);
  - a hash collision in `mame.exe` with the first, weaker mix;
  - a real 16-byte coincidence in `mame.exe`.
- **Measured:**
  - 0 matches in the distributable executable, SDL3, MAME, ImHex and Python;
  - it finds about 70-87 % of random 32-256 byte excerpts. Code and sound are found almost
    always; graphics with few colours are found worse, because their runs have little variety.
- **The primary guarantees are elsewhere:**
  - a text-only whitelist and the pre-commit hook for the repository;
  - a release built without the recompiled program.
- **Result:** the whole history and the release zip are clean.

## Publishing steps

1. `gh auth login` (the user, once).
2. Create the repository and push the full history.
3. Check the CI run.
4. Create release v1.0.0 with the zip.
