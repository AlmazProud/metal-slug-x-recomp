# Phase 9: universal loader (bring your copy, play)

**State:** closed, pending approval.

The port no longer needs the GOG installer: it recognizes the user's copy wherever it is and in
the layouts people actually have, and the distributable executable contains no code derived from
the game.

## What the port accepts

| Copy | Layout | Recognized by | State |
|---|---|---|---|
| GOG | DotEmu files `resources\game\mslugx_*` | name + SHA-256 | verified (the copy the port was built and checked with) |
| Steam (DotEmu port, same files most likely) | same | name + SHA-256 | not verified: no Steam copy was available. Files of the right size with other hashes still load, marked "unverified" |
| Arcade board dump (MAME set) | `mslugx` + `neogeo` as zips or loose files, any names | CRC-32 + SHA-1 from `mame -listxml mslugx` | loader verified with the set rebuilt from GOG (`tools/extract_roms.py`): same bytes as the DotEmu files. The arcade P1 (CRC `81f1f60b`) differs from GOG's and could not be run here |

- **Where it looks** (`src/port/find_game.c`, `window.c`), in this order:
  1. the path given (`--roms`, or a folder or zip dropped on the executable);
  2. the remembered one;
  3. `game\` and `roms\` next to the executable;
  4. the GOG registry entry;
  5. Steam libraries (`SteamPath` + `libraryfolders.vdf`, any `steamapps\common\*` with the
     DotEmu files);
  6. the usual install folders.
- **Nothing found:** a screen (SDL's built-in font, since the game's font comes from the copy;
  English or Spanish) asks the user to drop the folder or zip on the window or press ENTER for a
  folder picker.
- **Folders are searched up to 4 levels deep**, so the GOG/Steam install root, its
  `resources\game`, a MAME roms folder or a folder holding the two zips all work. With only
  `mslugx.zip`, `neogeo.zip` is looked for next to it.
- **MAME BIOSes:** the US MVS BIOS is preferred (it is the GOG copy's BIOS). Otherwise any
  other 128 KB MVS BIOS MAME lists is used: Europe, Asia, Japan, Universe BIOS. The 512 KB MV1C
  BIOSes are not supported. `sm1.sm1` is not needed: as with GOG, the Z80 boots from M1.

## Code (`src/neogeo/`)

- **`roms.c`:** the catalog (hashes only) and candidate gathering (folders and zip members
  filtered by name or size).
  - DotEmu first, then MAME.
  - Conversions from MAME's formats are the inverse of `tools/extract_roms.py`: the S ROM to
    4 bpp rows, and C ROM pairs (bitplanes) to DotEmu tiles.
  - `NgRoms.info` holds the format, the program version (GOG / arcade / unknown), the BIOS, a
    verified flag, the SHA-256 of P1 and BIOS, and a description.
- **`unzip.c`:** our own zip reader and inflate (RFC 1951, Huffman with a 9-bit table), with the
  CRC checked. No third-party library.
- **`hashes.c`:** CRC-32 and SHA-1.

## Recompiled or interpreted

- `program.py` writes into the generated program the SHA-256 of the P1 and BIOS it was generated
  from (`program_p1_sha256`, `program_bios_sha256`).
- `machine_start` uses the recompiled program only with exactly that copy. Any other version or
  BIOS runs the 68000 in the interpreter. `PD_INTERPRETER=1` forces the interpreter.
- **Distributable build:** `-DPD_RECOMPILED=OFF` (also automatic without ROMs or listings)
  builds `cpu/program_none.c`, so the executable carries no game code. It is 1.3 MB against
  17 MB for the recompiled build.
  - Audited: 21,698 random 16-byte pieces of every ROM file, 0 found in the executable.
  - The port now also builds without the user's copy (the tests are then skipped).

## Game profile per version

- `src/port/profile.c`: the cheats, debug options and red blood rely on addresses of the GOG
  program (`profile.h`).
- They are offered only when the code at every address they use (11 windows of P1, one SHA-256)
  is the same as in GOG. Otherwise the menu says "Unrecognized game version: cheats and debug are
  off" and nothing is forced. The game itself plays the same.
- Simulated with a modified P1 (a MAME set copy):
  - one byte changed outside the windows: unknown program, interpreter, profile still
    supported;
  - one byte changed in the spawn routine: profile not supported.
- The F3 debug panel shows the loaded copy and whether the 68000 is recompiled or interpreted.

## Verification

`ctest`:
- **Personal build:** 20 tests.
- **Distributable build:** 18 tests (no lockstep, which needs the recompiled program).

New tests:
- `roms_mame_folder` and `roms_mame_zip`: the MAME set, loose and zipped (`tests/loader/zip_set.py`
  makes the zips in the build folder), loads byte-identical data to the DotEmu files.
- `mission1_interpreter_vs_mame`: Mission 1 with the interpreter only, against MAME: identical.

Interpreter-only result: Mission 1 and the 10 stage-select runs of Phase 8 match MAME exactly
(the whole suite passes in the distributable build).

## Also

- CMake takes Python from PATH (`Python3_FIND_REGISTRY NEVER`), so the environment's
  `..\tools\python` is used instead of a system Python found in the registry.
- Headless: `PD_ROMS_INFO=1` prints what was loaded; `PD_COMPARE_ROMS=OTHER` compares two copies;
  `PD_PROFILE_HASH=1` prints the profile windows hash. Window: `PD_FIND_TEST=file.png` draws the
  "game not found" screen once.

## Pending / risks

- **Steam:** not verified (no copy). If its files differ, it still plays (interpreter, cheats
  only if the code matches).
- **Arcade P1:** not verified. It plays in the interpreter. Whether the cheats work depends on
  the profile windows check, which cannot be tried without that file.
- **512 KB BIOSes** (MV1C) and 7z sets are not supported.
