# Phase 11: Metal Slug X on the shared tree of the Metal Slug ports

**State:** closed, pending approval.

The Metal Slug 3, 5, 4 and Metal Slug ports grew out of this repository. Each step made the
runtime and the frontend more generic and added features. Metal Slug X was kept in those trees
as a regression target (`mslugx.exe`, its tests unchanged), but its own repository stayed at
v1.0.0. This phase moves Metal Slug X onto that shared tree.

## What was imported

The tree of the Metal Slug port (`MS1_Port\repo`, commit `e4051ca`) was copied in one commit,
unchanged (`7e6a79b`).
- **Left out:** the other games' code (`src/games/mslug*` other than `mslugx`, their `.rc`), their
  docs, and their encryption / banking tools (`learn_*`, `sma_banks`, `pvc_banks`,
  `probe_decryption`, `rebuild_set`, `make_reference`, their MAME scripts).
- **Kept from Metal Slug X:** its docs (`PHASE*.md`, `ROM_MAP.md`, `functions_p1.csv`), `README.md`,
  `CLAUDE.md`, `LICENSE` and `extract_roms.py`.
- **Architecture:** described in `docs/QOL_FEATURES.md` (shared features) and in the other ports'
  `PHASE1_IMPORT.md`.
  - `src/games/mslugx/game.c`: the descriptor (hashes, cartridge `NG_CART_ALTERA_MSLUGX`).
  - `src/games/mslugx/profile.c`: the profile (the old `PROFILE_*` values as a `GameProfile`).
  - `src/games/mslugx/sounds.c`: the sound names.
  - The shared runtime (`src/neogeo`) and frontend (`src/port`).

## Adapted (`39581d0`)

- **CMake:** `mslugx.exe` is the only game, with the old tests on `PD_ROMS` / `PD_ANALYSIS`
  (label `mslugx`), plus `mslugx_roms_dotemu` and `mslugx_find_steam` from the shared tree.
- **Version 2.0.0:** the save state format changed, so states saved by 1.x do not load.
- **Tools:** defaults to `mslugx`.
  - `mame_run.py` knows Metal Slug X's menu steps.
  - `make_portable.py` and `package_release.py` build Metal Slug X's packages.
  - `extract_roms.py` keeps Metal Slug X's rebuild and gives `check_load.py` its
    `sprites_dotemu_to_region`.
- **Setup:** copies `mslugx_*` from `K:\Metal Slug X`, keeps the MAME set in `roms\mame`, and
  `record_game.bat` records `mslugx`. The workspace README was rewritten.
- **THIRD_PARTY.md:** adds stb_truetype, the menu fonts Black Ops One and Chakra Petch (SIL OFL)
  and the modified ymfm (ADPCM-B as MAME's).

## What Metal Slug X gains (no game-specific data needed)

- **Menus:** the remastered menus with mouse support.
  - Start: Play, Game modes, Load, Settings, Extras, Quit, and Continue once there is a resume
    point.
  - Settings tabs: Video, Audio, Game, Keyboard, Gamepad, System.
  - Configurable shortcuts.
- **Continue:** automatic save every minute and on quit.
- **Gamepads:**
  - stick directions;
  - each player's gamepad remembered;
  - gamepad type (Xbox, PlayStation, Nintendo, arcade stick) and four profiles.
- **Video:** CRT effect, higher resolutions and integer scaling, PNG screenshots.
- **Speed and latency:**
  - run-ahead and lower input latency;
  - fast forward;
  - fast boot (`boot_frames` 304);
  - pause when the window is inactive.
- **Portable mode:** a `save` folder next to the executable.
- **Accuracy fixes from the other ports:**
  - MOVEM video write timing;
  - watchdog reset;
  - Z80 interrupts and command timing;
  - SSG soft reset;
  - ADPCM-B as MAME's;
  - NMI and YM2610 IRQ acknowledge;
  - sprite tile wrap.

## Verification

- **`ctest`:** 23/23 in the personal build, 21/21 in the distributable build. Mission 1 and the 11
  stage-select runs are identical to MAME, in both the recompiled program and the interpreter.
- **Stage select entry 2 now matches MAME** (the MOVEM video write timing) and is a test
  (`stage02_vs_mame`).
- **Entry 6** still diverges at VBlank 5071: the 68000 / Z80 timing, unsolved in every port.
- **User data:** with a fresh preference folder and with a copy of the user's 1.x `mslugx.ini`,
  the window boots to the new start menu and the old settings are read (fullscreen, Pixel filter).
- **Packaging:** `tools/package_release.py` builds `metal-slug-x-recomp-v2.0.0-win64.zip`
  (13 files, with the font and stb licenses), and the audit finds it clean.
- **History:** `tools/audit.py history` is clean (900 files).

## Not yet (Phases 12 and 13)

The shared frontend hides these for Metal Slug X because its profile has no mission, score or
boss data:
- Mission mode;
- Records and the Arcade results page;
- the Continue panel's mission and score;
- the "Start mission" cheat;
- the boss health bar;
- Boss Rush.
