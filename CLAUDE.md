# CLAUDE.md: Projecto Decompile (native Metal Slug X port)

Native Windows port of Metal Slug X (Neo Geo MVS, 1999) by **progressive static
recompilation**: the 68000 code is translated to C, the Neo Geo hardware is reimplemented as a
native runtime, and the recompiled functions are then gradually replaced with readable C.

Since Phase 11 the code is the **shared tree of the Metal Slug ports** (imported from the Metal
Slug port, `MS1_Port\repo` commit `e4051ca`), with Metal Slug X as its only game:

| Port | Public repository | Local copy |
|---|---|---|
| Metal Slug X (this one) | https://github.com/AlmazProud/metal-slug-x-recomp | `C:\Projecto Decompile\repo` |
| Metal Slug 3 | https://github.com/AlmazProud/metal-slug-3-recomp | `C:\Projecto Decompile\MS3_Port\repo` |
| Metal Slug 5 | https://github.com/AlmazProud/metal-slug-5-recomp | `C:\Projecto Decompile\MS5_Port\repo` |
| Metal Slug 4 | https://github.com/AlmazProud/metal-slug-4-recomp | `C:\Projecto Decompile\MS4_Port\repo` |
| Metal Slug | (not published yet) | `C:\Projecto Decompile\MS1_Port\repo` |

The other ports are **read-only from here**: code is copied from them (MIT, Copyright (c) 2026
AlmazProud) and adapted. Their `docs/QOL_FEATURES.md`, `BOSS_RUSH.md` and `PHASE4_PROFILE.md` are
the references for the features that need game data (missions, records, bosses, Boss Rush).

## Mandatory rules

1. **Bring your own ROM.** The repo never contains ROMs, BIOS, graphics, audio or extracted
   data. They are read from `..\roms\` at run time and validated by hash. The `.gitignore` is a
   whitelist: it ignores everything and only lets code and documentation through.
2. **MAME only as hardware documentation.** No MAME code is copied. Permissively licensed
   libraries are preferred (for example Musashi or ymfm), each documented in `THIRD_PARTY.md`.
3. **MAME is the oracle.** All behaviour is validated against MAME with traces or state
   comparison. Nothing is assumed without verification.
4. **Stack:** C17, CMake, Ninja, llvm-mingw and SDL3, all taken from `..\tools`. Analysis tools
   are written in Python 3.
5. **Language:** the assistant always talks to the user in Spanish. Everything in the
   repository (code, identifiers, comments, docs, commit messages) is written in English.
6. This file is updated when each phase closes.
7. **Nothing public** (each push, each release) without the user's explicit OK for that step.
8. Analysis tools run with `tools\python\python.exe` (the system's `python` is a Store stub).
   No downloads without asking the user; web searches for official names (bosses, weapons) only
   with the user's permission.
9. **The user's saved games:** window tests never use the portable folder or `%APPDATA%`; they
   use fresh preference folders in the session's scratchpad with `PD_PREF` (trailing slash).
10. **The GOG install is read-only** (`K:\Metal Slug X`): its files are copied with
    `INSTALL.bat -CopyRoms`.
11. A fix that only Metal Slug X needs goes in its profile (`src/games/mslugx/profile.c`) or
    behind a descriptor option, so the shared code stays identical to the other ports'.

**Way of working:** one phase at a time. Before writing code, present a short plan and wait for
the OK. When a phase closes, report what works, what is missing, the risks and the next step,
and wait for approval. Small commits with clear messages. Every uncertainty (format,
protection, timing) is investigated and verified against MAME.

## Structure

```
C:\Projecto Decompile\
  tools\     portable tools               roms\   user's ROMs (outside the repo)
  analysis\  analysis output              logs\   logs
  repo\      this repository
  INSTALL.bat, env.ps1, env.bat, README.md   (copies from repo\setup\)
repo\
  setup\        installer (setup.ps1, versions.json, env.*, record_game.bat, ci_tools.py, sdl3_test\)
  src\cpu\      dispatcher, 68000 state, Musashi fallback
  src\neogeo\   Neo Geo runtime (bus, LSPC, RTC, scheduler, ROMs, sound), shared by the ports
  src\port\     SDL3 frontend (menus, configuration, headless mode), shared
  src\games\mslugx\  game.c (NgGame descriptor), profile.c (GameProfile), sounds.c (names)
  tools\        analysis, recompiler (recomp68k\), MAME Lua scripts (mame\), verification,
                mame_run.py, audit.py, check_commit.py, hooks\ (pre-commit)
  tests\        differential\ (per instruction), lockstep\ (whole program), loader\
  external\     Musashi, ymfm, z80.h (chips\), stb_truetype, fonts
  docs\         per-phase documentation and ROM map; ADPCM_B.md, QOL_FEATURES.md (shared code)
```

Docs: `docs/ROM_MAP.md`, `docs/PHASE1_COVERAGE.md`, `docs/PHASE3_RUNTIME.md`,
`docs/PHASE4_VERIFICATION.md`, `docs/PHASE5_NATIVE.md`, `docs/PHASE6_REMASTER.md`,
`docs/PHASE7_TWEAKS.md`, `docs/PHASE7B_DEBUG.md`, `docs/PHASE8_STAGES.md`, `docs/PHASE9_LOADER.md`,
`docs/PHASE10_RELEASE.md`, `docs/PHASE11_SHARED.md`, `docs/PHASE12_MISSIONS.md`, `docs/BOSS_RUSH.md`; shared: `docs/QOL_FEATURES.md`,
`docs/ADPCM_B.md`.

Published at https://github.com/AlmazProud/metal-slug-x-recomp (MIT). Before every publication:
`python tools/audit.py history` and `python tools/package_release.py` (it audits the zip). The
GitHub CLI is in `tools\gh` (on PATH with the environment; login already done by the user).

Build: `cmake -S repo -B repo\build -G Ninja -DCMAKE_BUILD_TYPE=Release` and
`cmake --build repo\build`. The recompiled program is generated in `build\src\program_mslugx\` from
`roms\` and `analysis\`. Play: `repo\build\src\mslugx.exe` (finds the user's copy on its own:
`game\` next to the exe, GOG, Steam, or a dropped folder / zip). Tests: `ctest` in `repo\build`.
Distributable build (no code derived from the game, interpreter only):
`cmake -S repo -B repo\build-dist -G Ninja -DCMAKE_BUILD_TYPE=Release -DPD_RECOMPILED=OFF`.
Build folders always go in `repo\build` or `repo\build-*` (ignored): a configure into `repo\`
itself would generate the recompiled program inside the source tree.

Activate the environment: `. "C:\Projecto Decompile\env.ps1"` (PowerShell) or
`call "C:\Projecto Decompile\env.bat"` (cmd).

## Status by phase

| Phase | Status |
|---|---|
| Environment | **Closed** (2026-10-10). Verification 16/16 OK. ROMs copied to `roms\`. |
| 0. Reconnaissance | **Closed** (2026-10-11). See `docs/ROM_MAP.md`. |
| 1. 68000 analysis | **Closed** (2026-10-11). See `docs/PHASE1_COVERAGE.md`. |
| 2. Recompiler | **Closed** (2026-10-11). `ctest`: 3/3. |
| 3A. Runtime: video and controls | **Closed** (2026-10-11). See `docs/PHASE3_RUNTIME.md`. |
| 3B. Runtime: sound (Z80 + YM2610) | **Closed** (2026-10-11). See `docs/PHASE3_RUNTIME.md`. |
| 4. Boot and differential verification | **Closed** (2026-10-11). See `docs/PHASE4_VERIFICATION.md`. |
| 5. Native improvements | **Closed** (2026-10-11). See `docs/PHASE5_NATIVE.md`. |
| 6A. Remaster: autofire and debug mode | **Closed** (2026-10-12). See `docs/PHASE6_REMASTER.md`. |
| 6B. Remaster: sound catalog, sound menu, rumble | **Closed** (2026-10-12). |
| 6C. Remaster: modern start menu | **Closed** (2026-10-12). `ctest`: 6/6. |
| 6D. Remaster: widescreen | **Dropped** by the user (2026-10-12): it takes away the essence of the game. |
| 7A. V-sync and red blood | **Closed** (2026-10-12). See `docs/PHASE7_TWEAKS.md`. `ctest`: 6/6. |
| 7I. English translation | **Closed** (2026-10-12). `ctest`: 6/6. Publishing on GitHub (license, plug-and-play release) comes at the end. |
| 7B. Debug mode and cheats | **Closed** (2026-10-12). See `docs/PHASE7B_DEBUG.md`. `ctest`: 6/6. |
| 8. Every stage against MAME | **Closed** (2026-10-12). See `docs/PHASE8_STAGES.md`. `ctest`: 16/16. |
| 9. Universal loader | **Closed** (2026-10-12). See `docs/PHASE9_LOADER.md`. `ctest`: 20/20 (distributable build 18/18). |
| 10. Publishing on GitHub | **Closed** (2026-10-12). Public repository. See `docs/PHASE10_RELEASE.md`. Release v1.0.0. |
| 11. Adopt the shared tree of the Metal Slug ports | **Closed** (2026-10-26). See `docs/PHASE11_SHARED.md`. `ctest`: 23/23 (interpreter-only 21/21). Version 2.0.0 (not published). |
| 12. Profile A: missions, score, records, Mission mode | **Closed** (2026-10-26). See `docs/PHASE12_MISSIONS.md`. `ctest`: 33/33 (interpreter-only 31/31). |
| 13. Profile B: bosses, health bar, Boss Rush | **Closed** (2026-10-26), pending approval. See `docs/BOSS_RUSH.md`. `ctest`: 53/53 (interpreter-only 51/51). |
| 14. Publishing v2.0.0 | Planned. |

### Phase 13: result

Details in `docs/BOSS_RUSH.md`. All with bots (the user's choice).

- **Objects:**
  - a circular list from `0x1010A4` (`+4` previous, `+8` next, `+0xC` parent, `+0` the routine);
  - life `+0x4E`, dead bit 7 of `+0x5F`.
- **The game's kill request `0x1067E3`** is copied each frame to `0x1067E2`, which every enemy's
  hit check obeys (like "D kills all"). Marking objects breaks Metal Slug X.
- **New shared fields:**
  - `kill_flag`, used by the fast-forward and Boss Rush's test defeat;
  - `start_section_at` and `ProfileRushState.section`.
- **New tools:** `PD_KILL_FLAG` in the port and in `scripted_run.lua`, `PD_TRACE_LIFE`,
  `PD_SECTION_ONCE`.
- **Bosses** (found by fighting from saved states):

  | Mission | Code | Life |
  |---|---|---|
  | 1 | `0x0D5A92`-`0x0D67E6` | 5000 / cannon 700 |
  | 2 | `0x026354`-`0x026FCE` | 3500 |
  | 3 | `0x04A380`-`0x04ACDC` | 5500 |
  | 4 | `0x0235E8`-`0x0241CC` | 7 guns, 500 each |
  | 5 | `0x0A0C9A`-`0x0A0EE2` | 5000 |
  | 6 | `0x0415BA`-`0x0417FE` | 6500 |

  No names yet: a web search needs the user's OK.
- **Boss Rush:** saved states made by the fast-forward (about 84 s the first time). Mission 2
  starts from section 12, because the pyramid stops every bot.
- **Against MAME** (`analysis/phase13`): 12 runs with every mission played to its end, and the
  bosses fought. All identical, about 254 000 frames.

### Phase 12: result

Details in `docs/PHASE12_MISSIONS.md`.

- **Addresses:**
  - mission `0x10C2F5` (0-5; 6 after the final mission);
  - stage `0x10C2F6`;
  - stage set / section to start at `0x10C2FC` / `0x10C2FE`, copied at the load to
    `0x10C2F8` / `0x10C2FA`;
  - score `0x10C46A` (4 BCD bytes);
  - PLAY MANUAL, the high byte of `0x10FD8E`.
- **The stage select:** entries 1-6 are Missions 1-6, 7-12 Missions 1-6 again in other stages, 13
  the ending.
- **Starting at a mission:** both the stage set index and the mission are written, with a new
  shared profile field `start_mission_also` (0 elsewhere).
- **Against MAME** (`analysis/phase12`): Missions 2-6 started with pokes at frame 704, the "Start
  mission" option, and the bot finishing Mission 1 are all identical.
- **Port:** Mission mode's direct start, progress (Mission 2 unlocked) and records (not clean with
  the dips), checked in the window.
- **Open:** a clean recorded game, and the game's own time-attack mode (`PINPOINT ATTACK`).

### Phase 11: result

Details in `docs/PHASE11_SHARED.md`.

- **Import:** the Metal Slug port's tree (`e4051ca`), unchanged, in one commit (`7e6a79b`),
  without the other games' code, docs and encryption tools. Then mslugx was made the only
  product (`39581d0`): CMake, tests, tool defaults, setup, packaging, THIRD_PARTY.md.
- **New for Metal Slug X:**
  - remastered menus with mouse, Continue, configurable shortcuts and gamepad pictures;
  - run-ahead, CRT effect, fast boot and fast forward, portable mode;
  - the other ports' accuracy fixes.
  - Mission mode, records, boss bar and Boss Rush need profile data (Phases 12 and 13).
- **Stage select entry 2 now matches MAME** (the shared MOVEM timing): `stage02_vs_mame`. Entry 6
  still diverges (Z80 timing, unsolved in every port).
- **Version 2.0.0:** 1.x save states do not load; the user's `mslugx.ini` is read as before.

### Phase 10: result

Details in `docs/PHASE10_RELEASE.md`.

- **License and docs:** MIT `LICENSE`, `README.md` in English, version 1.0.0 (`version.h`, also
  in the executable's properties).
- **Audits:** `tools/audit.py` checks the history and the release zip: whitelisted text, size,
  encoded data, and an exhaustive search for runs copied from the ROMs. The whole history is
  clean.
- **Packaging:** `tools/package_release.py` builds the release zip from the interpreter-only
  build.
- **CI:** GitHub Actions builds the distributable executable with the pinned toolchain
  (`setup/ci_tools.py`).
- **GitHub CLI** is in the installer (`versions.json`).

### Phase 9: result

Details in `docs/PHASE9_LOADER.md`.

- **Loader:** `src/neogeo/roms.c` takes a folder or a zip.
  - **DotEmu files** (GOG; Steam most likely): validated by SHA-256.
  - **MAME set** (`mslugx` + `neogeo`, zips or loose files): matched by CRC-32 + SHA-1 from
    `mame -listxml`. Graphics are converted to the DotEmu layout (the inverse of
    `extract_roms.py`).
  - Our own zip/inflate (`unzip.c`), CRC-32 and SHA-1 (`hashes.c`).
- **Finding the copy** (`src/port/find_game.c`): `--roms` or a path dropped on the exe, the
  remembered one, `game\` or `roms\` next to the exe, GOG, Steam libraries, then usual folders.
  If nothing is found, a bilingual screen accepts a dropped folder or zip, or ENTER for a folder
  picker.
- **Recompiled program:** used only with the exact P1 and BIOS it was generated from (their
  SHA-256 is embedded by `program.py`); otherwise the interpreter runs.
  - `-DPD_RECOMPILED=OFF` gives the distributable exe (1.3 MB, no game code; audited).
  - The interpreter-only suite matches MAME in Mission 1 and the 10 stage runs.
- **Profile check:** `src/port/profile.c` hashes 11 windows of P1. Cheats, debug and red blood
  are only offered when they match the GOG code.
- **Not verified for lack of files:** Steam's copy and the arcade P1. They load and play in the
  interpreter.
- **Also:**
  - CMake takes Python from PATH (`Python3_FIND_REGISTRY NEVER`).
  - `.gitignore` ignores `build-*/`.
  - A folder named `roms/` is always ignored, which is why the zip test helper lives in
    `tests/loader/`.

### Phase 8: result

Details in `docs/PHASE8_STAGES.md`.

- Scripted runs of the developers' stage select in MAME and the port (`tools/stage_run.py`,
  `tools/mame/scripted_run.lua`; MAME `-record`, clock date from the `.inp` header, `--replay`).
- 10 of the 12 stage select entries (3-5, 7-13) match MAME exactly over 9,000 frames: tests
  `stageNN_vs_mame`. Entry 2: RAM identical, 3 images differ on one line. Entry 6: Z80 reply
  timing (the pending sample-accurate Z80 item).
- Fix: the line counter `0x3C0006` is sampled 6 cycles before the end of the reading
  instruction.
- The user's sound names in the profile; weapons 12/15 renamed (Enemy Chaser / Super Grenade).

### Phase 7B: result

Details in `docs/PHASE7B_DEBUG.md`.

- The game's own debug tools: development mode (`0x10FE80` and its copy `0x10C2BE` forced for
  cartridge reads) opens the developers' menu with SELECT + D twice in a mission (port: F4 or
  Extras > Debug); debug dips at `0x10F000` mapped bit by bit.
- RAM cheats in `src/port/cheats.c` (addresses in `profile.h`): weapons 0-15 through the
  pickup routine's ROM tables, bombs, lives, soldier (on respawn), form (fat / mummy; fat kept
  with a forced read limited to the PC range of 0xC0BAE: `NgForcedRead.pc_first/pc_last`).
- Vehicles: the stage's object record routine (`0x1A0A6`) run as a nested call
  (`ng_call68k`) on a VBlank wait; 12 vehicle types found (`0x2EC`-`0x318`).
- Menus Extras > Cheats and Extras > Debug (EN/ES). Tests: `PD_CHEAT`, `PD_WINDOW_TEST=...,cheats|debug`.

### Phase 7I: result

- Everything in the repo is English: directories (`tools/`, `setup/`, `external/`,
  `tests/differential/`), files, identifiers, comments, docs, env vars (`PD_*`), command-line
  options and test names. Workspace analysis data moved to `analysis\` with English names.
- User data stays compatible: `mslugx.ini` and the sound names file still load their old Spanish
  keys (written back in English); `sonidos.ini` -> `sounds.ini` and `estado_N.bin` ->
  `state_N.bin` are renamed automatically; headless scripts accept the old Spanish tokens.
- Frontend language option (`src/port/lang.c`, `T()`): English by default, Spanish with
  `language=es` (Settings -> Language, first item).
- Release idea for later (GitHub phase): ship an executable without game code that runs the
  interpreter only (483 frames/s measured, 8x real time) and finds the GOG install or accepts a
  dropped folder; the recompiled build stays a build-from-source option.

### Phase 7A: result

Details in `docs/PHASE7_TWEAKS.md`.

- **Red blood:** it is the BLOOD option of the game's soft dips (OFF in the US), at
  `0x10FD8F`; the game reads it at `0x1B2BE`.
  - The port option forces only the game code's read (`ng_forced_read`, PC < `0xC00000`).
    Writing the RAM broke the BIOS test.
  - Checked with screenshots: white splatter vs. red.
- **V-sync:**
  - with a ~60 Hz monitor, one frame per refresh, with audio kept in proportion and corrected
    from the queue;
  - with other refresh rates, an accumulator at the board's rate.
  - Measured: 0 audio dropouts and 0 discards in all three cases.
- Tests: `PD_QUIT_AFTER`, `PD_HZ`, `--blood`.

### Phase 6C: result

Details in `docs/PHASE6_REMASTER.md`.

- **Font** (`src/port/font.c`): the game's shaded 8x8 font (cartridge fix, tiles `0x300` +
  ASCII), read from the user's copy and drawn in uppercase with our own inks.
- **Paged menus** (`src/port/menu.c`, interface `frontend.h` towards `window.c`):
  - Start: the demo keeps running in the background; "Play" inserts a coin + START.
  - Pause, Settings (Video, Audio, Game, Controls) and Extras.
- **Sound menu:** Recent tab, rename (F2/R) and per-sound rumble (V/X), saved in the user's
  `sounds.ini`.

### Phase 6B: result

Details in `docs/PHASE6_REMASTER.md`.

- **Sound driver:** mode commands `00` (music), `10` (effects) and `11` (voices) that stay
  active; `03` resets the driver (the game sends `03 03 06` before each music track).
- **Catalog:** `--sound-catalog` + `PD_CATALOG_PREFIX`; `tools/sound_catalog.py`
  generates `src/port/profile_sounds.c` (18 music tracks, 185 effects, 61 voices).
- **Names:** only those confirmed with gameplay screenshots; the rest go by number.
- **"Music and sounds" menu** (`src/port/sounds.c`): saves the state, advances only the Z80 and
  the YM (`ng_sound_only_*`) and restores on exit. Test `sound_menu`.
- **Rumble** driven by sound commands (`ng_on_sound_command`): explosions, cannon, shots;
  intensity 0-100.

### Phase 6A: result

Details in `docs/PHASE6_REMASTER.md`.

- **Autofire** per player: its own button (B / G / RB) or on the fire button; 10, 7.5 or 5 per
  second. The pistol never exceeds 10 shots per second; measured with sound command `F7`.
- **Debug mode** (F3, `src/port/debug.c`):
  - FPS;
  - logic Hz, from byte `0x10008A` (frames per cycle: 2 normal, 3 or 4 slowdown);
  - 68000 load (lines outside the wait loop `0x39B0`-`0x3A41`) and dropped frames;
  - graph;
  - F6 frame advance;
  - hide sprites or fix;
  - sprite boxes, palette and RAM.
- `src/port/profile.h`: Metal Slug X specific addresses.

### Phase 5: result

Details in `docs/PHASE5_NATIVE.md`.

- **Frontend** (`src/port/`): `main.c` (options), `window.c`, `config.c` (`mslugx.ini` in
  `%APPDATA%\ProjectoDecompile\mslugx\`), `gog.c` and `headless.c` (the Phase 4 tools, with no
  behaviour changes).
- **ROMs:** found automatically: `--roms`, the remembered folder, `roms\` next to the exe, the
  GOG registry entry (id 2046360890) or a folder picker.
- **Video:** 4:3 or square pixels; sharp filter (integer scale + linear), smooth or pixels;
  scanlines; fullscreen.
- **Menu** (Esc or Guide): save states in 9 slots (also F5/F9), slowdown on/off, volume,
  2-player controls (keyboard and gamepad), service menu (F2) and reset.
- **Turbo** (`CIC` in `cycles68k.h`, `cpu_turbo`): at x2, frames dropped to slowdown fall from
  10.3 % to 0.2 % (attract demo) and from 9.0 % to 0.0 % (Mission 1). When off it changes
  nothing.
- **Save states** (`neogeo/savestate.c`, 835 KB): test `savestate_roundtrip`; loaded into a reset
  machine, they reproduce identical RAM, video and audio.
- `cmake --install build --prefix X` leaves the exe + SDL3.dll. Manifest: UTF-8 paths and HiDPI.

### Phase 4: result

Details in `docs/PHASE4_VERIFICATION.md`.

- **Zero divergences in Mission 1:** the user's recording (11 969 frames) replays with identical
  RAM and palette in all 11 771 VBlanks and identical video in all 11 968 frames. Automatic
  test: `ctest` → `mission1_vs_mame` (references in `analysis/phase4/`, outside the repo).
- **68000 cycle accuracy against MAME:**
  - reset: the first instruction starts at cycle 34;
  - IRQ entry: waits for the E clock;
  - IRQ sampling: per-family rules, verified on 2091 cases;
  - fixes to Musashi's table: ADD.L with register, ADDQ.W to An, ADDA.W #, ANDI.L,
    BSET/BCLR/BCHG with bit < 16, MULS, exact DIVU/DIVS.
  - Everything lives in `src/cpu/cycles68k.h`, shared by the generated code, the interpreter and
    the tests.
- **Runtime:**
  - inputs recorded by time;
  - video write / line render order as in MAME;
  - reading the Z80 reply in the middle of an instruction;
  - YM2610 at exactly 55 555 samples/s (as MAME);
  - fixed: a YM timer was running line processing ahead.
- **Tools:**
  - in `mame/`: `fingerprints.lua`, `inputs.lua`, `cycles.lua` (debugger cycle trace),
    `trace_reads.lua` and `trace_ym.lua`;
  - `compare_fingerprints.py`, `verify_mission.py`, `compare_cycles.py` and `audit_cycles.py`;
  - `sampling_queues.py` + `verify_sampling.py` + `recomp68k/sampling_classes.c`.
- **Residual:** sound differs from MAME by 0-3 samples at the start of some effects (the Z80
  sometimes services the YM IRQ one instruction earlier). It does not affect the game.

### Phase 3B: result

- **Real sound:** Z80 (`external/chips/z80.h`, zlib) + YM2610 (`external/ymfm`, BSD-3, the same
  core as MAME) in `src/neogeo/sound.c`; `ym2610.cpp` is the only C++ in the port (static C++
  runtime: the executable only needs `SDL3.dll`). Audio through SDL3 at 55 555 Hz, 50 ms
  buffer, no dropouts.
- **Interleaving like MAME's scheduler:** the Z80 runs up to each 68000 timeslice boundary; a
  command syncs immediately and opens 50 µs of fine interleaving. Commands, replies and reads
  match MAME within a few µs up to frame 880.
- **Audio against `-wavwrite`:** correlation 1.00000 per segment (`tools/compare_audio.py`).
- **Frame numbering fixed:** MAME's screenshot N is the image completed while running frame N.
  With that, 448 of 451 consecutive frames match (previously 321).

### Phase 3A: result

Details and evidence in `docs/PHASE3_RUNTIME.md`.

- **Boot:** the port runs the user's MVS BIOS recompiled (not HLE), enters the game, and
  Mission 1 is playable with keyboard or gamepad (sound: Phase 3B).
- **Runtime (`src/neogeo/`):** full memory map, protection, LSPC (sprites, fix, shrinking,
  palettes, auto-animation, IRQ1/IRQ2/IRQ3), uPD4990A, watchdog and persistent SRAM. ROMs are
  read from the GOG folder and validated by SHA-256.
- **Against MAME:**
  - pixel-identical screenshots, colors included (current figures in Phase 3B);
  - I/O writes match in order and data, with an offset of about 1 µs.
- **Interrupts at instruction boundaries:** the generator places a checkpoint before every
  instruction. The dispatcher uses `rc_irq_level` to return to `rc_event` when the mask is
  lowered.
- **Frame phase as in MAME:** at power-on the beam is at vpos 240; the frame ends at VBlank.
- **Z80 stand-in** with measured latencies (command 1: 175 929 cycles, command 3: 729);
  replaced in 3B.
- **Tools:**
  - `compare_screenshots.py`, `images.py` (dependency-free PNG/BMP);
  - in `mame/`: `screenshots.lua`, `trace_ports.lua`, `trace_sound.lua`, `z80_latency.lua` and
    `trace_io.lua`;
  - in the port: `--headless`, `--screenshots`, `--script`, `--trace` and `--trace-io`.

### Phase 2: result

- **Musashi 4.60 (MIT) without SoftFloat** (user's option B): `external/musashi/` with our own
  stand-ins for `m68kfpu.c` and `softfloat/*.h`. It is the semantics reference and the fallback
  interpreter.
- **Own decoder** (`tools/recomp68k/decoder.py`): Capstone decodes 68020 formats in 68000 mode.
  Validated on 116 581 instructions; the 6 differences are Capstone bugs.
- **Generator** (`generator.py`): the 87 instructions the game uses, with 68000 cycle counts
  (Musashi's base table, dumped at build time, plus the dynamic adjustments).
- **Whole program** (`program.py`): the user's P1 and BIOS grouped into regions (connected
  components of the control-flow graph), each instruction emitted only once and entry through
  `switch(c->pc)`. 124 193 instructions, 3 318 regions, 31.7 MB of C in `build/`.
- **Dispatcher** (`src/cpu/dispatcher.c`): JSR/BSR = verified native call (`rc_call`);
  checkpoints on backward branches; Musashi fallback for RAM, P2 and unseen code.
- **Tests** (`ctest`):
  - `cpu_differential`: 4 253 real instructions (3 356 opcodes) × 64 states = 272 192 cases,
    0 differences in registers, SR, PC, memory and cycles. A mutation test confirms the harness
    catches errors.
  - `lockstep_reset`: whole program against Musashi from reset.
  - `lockstep_game`: from a real state (MAME dump at frame 4000 of `mision1.inp`): 120M
    cycles, 591 VBlanks, 10.1M instructions, identical memory, a single interpreted step. The
    recompiled program is about 5 times faster than Musashi.
- **User's recording:** `analysis/inp/mision1.inp`, 11 969 frames, up to the Sphinx. It replays
  deterministically (`tools/mame/final_state.lua`); an empty nvram is always used to replay it.
  Coverage with it: 40 404 P1 PCs.

### Phase 1: result

- **Tools:**
  - `tools/coverage.py` + `mame/coverage.lua`: dynamic coverage in MAME. Uses the debugger
    trace in chunks, an input bot, bank switches and data reads.
  - `tools/analyze68k.py`: recursive disassembly with Capstone, A/B heuristics, validation
    against MAME and comparison with Ghidra.
  - `tools/ghidra/*.java`: root seeding and headless function export.
- **Memory map:**
  - P1 is fixed at `0x000000`.
  - P2 is seen in 1 MB banks at `0x200000–0x2FFFFF`; switched with a write to `0x2FFFF0`
    (bank = data & 7). Banks 0–3 are used in the trace.
- **Entry points:**
  - VBlank (IRQ1) at `0x22AC`, timer (IRQ2) at `0x2322`.
  - USER `0x2432`, PLAYER_START `0x2658`, DEMO_END `0x2800`, COIN_SOUND `0x27F6`.
  - 68000 exceptions go to `0x22A2` (jump through `$10.w`).
- **Result (300 s trace):**
  - 26 406 P1 addresses executed.
  - 4 488 functions; 46 % of P1 is code (12.6 % confirmed and the rest heuristic).
  - Everything MAME executed is reached by the analysis.
- **Precision:** 130 heuristic bytes over proven data (heuristic A). Ghidra, by comparison,
  finds 55.5 % of what was executed without MAME; our own analysis, 51.3 %.
- **Code outside P1:**
  - 2 instructions in P2 (bank 1, `0x299178`, called from `0x44EC`);
  - a routine copied to RAM (`0x10FC00`, `jmp $7C00(a5)` at `0x10EA`).
  - Implication for Phase 2: the dispatcher must take the active bank into account and fall
    back to the interpreter for RAM and unseen code.

### Phase 0: result

- **Extractor:** `tools/extract_roms.py` rebuilds the MAME set in `roms\mame\` from the DotEmu
  files. 15 files match exactly (CRC32 + SHA1) with `mame -listxml`; MAME remains the source of
  the hashes.
- **GOG P1:** it differs from MAME's (CRC `60c33b1a`). Its origin is unidentified and will be
  investigated in Phase 1. The port and the oracle use the same P1, GOG's.
- **`sm1.sm1`:** GOG does not include it. It is replaced with M1, the way the AES boots.
  Verified: it boots, enters attract mode and sound works.
- **DotEmu formats:** fix and sprites come unpacked at 4 bits per pixel in row order. The
  conversion is reversible and documented.
- **Protection:** ALTERA at `0x2FFFE0`–`0x2FFFEF`, which returns bits from a table in P1
  (`0x0DEDD2`). Confirmed with `tools/mame/trace_protection.lua`: 127 206 of 127 206 reads
  match the model.
- **Starting the oracle:** `mame mslugx -bios us -rompath "<root>\roms\mame"`.
- **Pre-commit hook:** rejects binaries, files over 1 MB and ROM copies.

### Environment phase: result

Versions pinned in `setup/versions.json` (all with hashes verified against the official source):

| Tool | Version |
|---|---|
| 7-Zip | 26.04 |
| Python | 3.14.8 (NuGet) |
| CMake | 4.4.4 |
| Ninja | 1.13.2 |
| llvm-mingw | 20261006 (clang 23.1.3, UCRT) |
| SDL3 | 3.4.18 |
| Temurin JDK | 21.0.12.1 |
| Ghidra | 12.1.4 |
| MAME | 0.289 |
| ImHex | 1.38.1 |
| MinGit | 2.56.0.2 |

Verified under the path with a space: CMake, Ninja, clang, llvm-windres and SDL3 (builds and
opens the window), and Ghidra headless (imports a PE). Ghidra leaves nothing in `%APPDATA%`,
`%LOCALAPPDATA%` or `%TEMP%`.

ROMs: 13 `mslugx_*` files copied from GOG to `roms\`, with `roms\MANIFEST.sha256` verified.

## Lessons from the Metal Slug ports (apply them)

Collected in the Metal Slug, 3, 4 and 5 ports (their `CLAUDE.md`); the essentials:

- **Lua in MAME:** subscriptions and taps in global variables; a fresh nvram folder per run;
  Windows paths, not `/c/...`; MAME reruns the autoboot script after a soft reset (guard it with a
  global); write taps on the LSPC registers `0x3C0000-3` never fire: use the debugger trace
  (`beamx`/`beamy`, `totalcycles`). Big-endian regions compose words big-endian
  (`dump_region.lua` handles both).
- **MAME timing already reproduced in the runtime:** watchdog reset, MOVEM video write order, Z80
  interrupts two T-states early, 68000 commands one cycle into the access, MAME's ADPCM-B, SSG soft
  reset, an NMI that does not interrupt the YM2610's IRQ acknowledge. If Metal Slug X diverges,
  suspect these first.
- **Audio:** Metal Slug 4's and 5's are not exact because of the power-on alignment of 68000, Z80
  and YM2610 (Metal Slug 5's two fixes broke Metal Slug 3). Measure it in Phase 2 and tell the user
  what is seen before investing much; a generic fix would serve all five.
- **MAME's scheduler:** rare ~2-cycle shifts of raster interrupts are a known difference: bound
  them and ask the user before chasing them. **Known residue:** VBlanks where MAME takes the
  interrupt one instruction later, listed exactly per run (`*_known_vblank.txt`) after a
  byte-by-byte check; MAME's boot is not deterministic in some runs (the reference is the replay
  that matches).
- **Reaching sections without playing:** the start mission / section bytes poked during the
  selection (`PD_POKE`), the developers' stage table (names sometimes in half-width katakana:
  cp932). Metal Slug X has the developers' stage select (docs/PHASE8_STAGES.md).
- **Finding the engine:** `tools/match_windows.py` with Metal Slug X's profile, then check every
  address in the listing and in MAME. Assume nothing: identify weapons, forms, vehicles and every
  boss (middle ones too) in the game; what is not identified is listed by number.
- **Bosses:** phases (life back to 1), several pieces, life under 1000 or going negative
  (`boss_min_life`, `boss_life_signed`). The "D kills all" dip can stall a fight: use a mixed bot.
  **Boss Rush** and **Mission mode:** a RAM write hook (`ng_hook_word`) on the section start bytes,
  rewritten until loaded. **Records:** only clean games, with the notice of why one was not saved.
- **Sound:** the catalog taken on a silent frame; the driver modes found by watching `0x320000`
  (`PD_WATCH_W=320000`) in recorded games; the sound menu uses the game's own silence sequence.
- **Editing:** bash heredocs break `\n`, `\\` and `\b` in C and Python: write scripts with the
  Write tool. `.bat` and `.ps1` need CRLF. `python -I` blocks sibling imports.
- **CMake** only sees the lists and references that exist at configure time: reconfigure after
  creating them or re-recording a MAME reference; no nested lists in `foreach` (use functions);
  tests that write progress or records files need a fixture that deletes them first.
- **Window executable:** GUI subsystem, no stderr (`Start-Process -RedirectStandardOutput`, or
  debug to a file and remove it before committing). Window tests: `PD_WINDOW_TEST`, `PD_PREF`
  (fresh folder, `start_screen=0` in its ini to go straight in), `PD_LOAD_STATE`, `PD_FIND_TEST`.
  **Headless** needs `--roms <folder>` and the full date; RAM dumps (`PD_DUMP_V`) need
  `--fingerprints`; MAME's `PD_BLOOD` is `word,mask`; the VBlank number is not the frame number.
- **Deletions:** `rm` with variables is blocked by the harness: use `"${VAR:?}"` or fresh folders.
- **`gh`:** AlmazProud's session lives in `GH_CONFIG_DIR = C:\Projecto Decompile\tools\_cache\gh`
  (set by `env.ps1`); PowerShell breaks `--jq` (use `--json` and `ConvertFrom-Json`);
  `gh release create --target` needs the full SHA; check the SHA-256 of the downloaded release zip.
- **Packaging:** `tools/package_release.py` refuses a recompiled build and needs `PD_ROMS`; test the
  unzipped release in an empty folder with the game in `game\`, and a clean clone with only the
  toolchain and Python on PATH. Portable: `tools/make_portable.py --only <id>`.
- **Run-ahead:** disable every hook (`ng_before_input_read`, `ng_on_sound_command`,
  `ng_hook_word`, `ng_on_vblank_ack`), no audio in the frames run ahead, run it after queueing the
  real audio.

## Decisions

- **Python from NuGet** instead of the embeddable package: it ships pip and venv without
  patches. Python 3.14 because 3.15 was too recent.
- **The installer is versioned in `repo/setup/`.** The root `INSTALL.bat` calls it from there,
  and the root is computed as the parent of `repo`.
- **Portable MinGit** instead of the system Git, so everything stays in a single folder.
- **Calling `.bat` files with paths containing a space:** use `cmd /d /s /c "..."` (function
  `Invoke-Proceso` in setup.ps1). Otherwise cmd strips the outer quotes and the path breaks.
- **The Python hash is SHA512**, because NuGet only publishes that one. Everything else uses
  SHA256.
- **Git identity** configured only in this repo (`git config --local`).
- **Portable Ghidra through the 8.3 short path** (option B). `setup.ps1` writes into
  `launch.properties` the properties `application.settingsdir`, `cachedir`, `tempdir` and
  `java.io.tmpdir`, pointing to `C:/PROJEC~1/tools/_cache/ghidra/...`. The short path is
  recomputed on every run. If the drive has no 8.3 names, the installer warns and Ghidra uses
  its default paths. Quoting (option A) was rejected because `LaunchSupport` takes the quotes
  literally.
- **Text files:** `.bat` and `.ps1` use CRLF (pinned in `.gitattributes`). `.ps1` files carry a
  UTF-8 BOM, because PowerShell 5.1 reads them as ANSI without one.

## Findings

- **GOG install** at `K:\Metal Slug X`. It does not ship a MAME set: they are DotEmu files in
  `resources\game\` (`mslugx_game_m68k` 5 MB, `mslugx_tiles` 48 MB, `mslugx_adpcm` 10 MB,
  `mslugx_game_z80`, `mslugx_game_sfix`, `mslugx_bios_m68k`, `mslugx_bios_sfix`,
  `mslugx_zoom_table`, among others). Most likely they do not match MAME's `mslugx` set byte for
  byte, and MAME's format will have to be rebuilt in Phase 0.
- **Ghidra and spaces:** with its original configuration, Ghidra 12.1.4 works under a path with
  a space. What fails is that `launch.bat` (line 185) concatenates the `VMARGS` from
  `launch.properties` without quotes, and `LaunchSupport` takes the quotes literally. That is
  why `application.settingsdir`, `cachedir` and `tmpdir` cannot be redirected to a path with a
  space. Ghidra does honour `application.settingsdir` for `java_home.save` too.

## To do

- [ ] **Phase 14** (plan approved by the user, 2026-10-26): publishing v2.0.0. Open from Phase
      13: Mission 5's middle boss (the boss names were added, 2026-10-26). Open: stage select entry 6 (Z80 timing). Not verified
      for lack of files: Steam's copy and the arcade P1.
- [ ] **Later, in their own conversations (the user's decision):**
  - **Metal Slug 3 port.** GOG copy at `K:\Metal Slug 3` (GOG id 1370911530), same DotEmu
    layout: `resources\game\mslug3_*`, with a 9 MB 68000 program and 64 MB of tiles.
    Expected work:
    - the SMA protection (bank switching of the 8 MB P2, random number generator);
    - CMC42-encrypted graphics and an SMA-encrypted program in arcade dumps;
    - first risk: the MAME oracle expects the encrypted cartridge ROMs.
    - Do the game profile split first (see below).
  - **PS Vita port.** C17 + SDL3 (SDL3 supports the Vita). The risk is CPU speed (444 MHz).
    Measure the interpreter on hardware first. Options if it is too slow: a pre-decoded
    interpreter, a JIT (allowed for Vita homebrew), or a PC tool that builds the `.vpk` from the
    user's copy.
- [ ] **Roadmap requested by the user** (for the final result):
  - [x] **Sonic-style debug (7B):** done (developer menu, debug dips, RAM cheats).
  - [ ] **Sound names and laser rumble:** the user identifies them with the Recent tab; move
        their names into the profile (`tools/sound_catalog.py`).
- [ ] **Sound names:** 12 music tracks and most voices and effects go by number. Recordings of
      other missions are needed (the attract mode has no music).
- [ ] **Game profile** to port other Metal Slug games (proposed by the user): split out what is
      mslugx-specific (`src/port/profile.h`, `roms.c`, protection in `bus.c`).
- [ ] **Attract demo not recompiled:** in 12 000 frames without input, 35 million steps go
      through the interpreter (recompilation coverage comes from Mission 1). Add that coverage
      if performance requires it; it still runs at about 1000 frames/s today.
- [ ] **Sample-accurate Z80 like MAME:** when MAME's Z80 services the YM2610 IRQ (first case at
      15.1 s into the recording, PC 0x0010). Today audio matches except for 0-3 sample shifts.
- [ ] **IRQ sampling without traces:** DIVU/DIVS with a memory operand, CHK, TRAPV, MOVEP.
- [ ] **Order of 32-bit writes:** `MOVE.L` to `-(An)` writes the low word first on the 68000
      and in MAME; the port always writes the high word first.
- [ ] **MAME timeslice boundaries the port does not use yet** for Z80 interleaving: for example
      the uPD4990A timers.
- [ ] **Undefined flags that follow Musashi:** N/V of ABCD/SBCD and N/Z on DIVU/DIVS overflow.
      They must be confirmed against MAME (DIV cycle counts are already exact).
- [ ] **DIVS 0x80000000/-1:** implemented as the hardware does (V=1); Musashi returns 0. If it
      shows up in a test, it is a known difference.
- [ ] **P2 code:** bank 1, 144 PCs; currently run by the interpreter. Recompile it per bank if
      performance requires it.
- [ ] Discard heuristic A roots whose code overlaps data proven by MAME (today: 130 bytes).
- [ ] More recordings (other missions) to extend Phase 4 verification beyond Mission 1.
- [ ] 99 indirect jumps or calls with no observed target (for example `0x22AA` and `0x2354`).
- [ ] Identify how GOG's P1 differs from MAME's (Phase 1).
- [ ] Lua in MAME: subscriptions (`add_machine_stop_notifier`, taps) must be kept in global
      variables; otherwise Lua silently garbage-collects them. Do not name a local variable
      `io`: it shadows Lua's `io` library. Each MAME run with an empty nvram needs a fresh
      folder: MAME saves the nvram on exit. `tracelog` does not work in the Z80 trace.
