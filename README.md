# metal-slug-x-recomp

## A word from the author

Hey everyone, real human here! Keep in mind that this was vibe coded, so it may have some
errors. This started as a personal project just to play MSX on my PS Vita, lol (which is still
on my to-do list), but I figured it could be a step forward for Metal Slug ports. I have no
prior coding experience, nor do I intend to learn, so feel free to use this however you like. I
asked Claude to add some things I wanted that I think could be useful.

If you find any bugs, please report them and I'll ask Claude to fix them c:

Enjoy!

---

A native Windows port of **Metal Slug X** (Neo Geo MVS, 1999), built by statically recompiling
the game's 68000 program to C and reimplementing the Neo Geo hardware around it.

**Bring your own copy.** This repository and its releases contain no part of the game: no code,
graphics, sound or BIOS. The port reads them at run time from a copy you own.

> Metal Slug X is © SNK CORPORATION. This is an unofficial fan project, not affiliated with or
> endorsed by SNK. Please buy the game.

## Quick start

1. Download the latest zip from [Releases](https://github.com/AlmazProud/metal-slug-x-recomp/releases)
   and unzip it anywhere.
2. Have a copy of the game. Any of these works:

   | Copy | What the port needs |
   |---|---|
   | **GOG** or **Steam** (the PC release) | Nothing: if it is installed, the port finds it on its own. Or copy its install folder into `game\`. |
   | **Arcade board dump** (MAME set) | `mslugx.zip` and `neogeo.zip` (or their files) in `game\` |

3. Run `mslugx.exe`. If it cannot find the game, drop the game's folder or zip on the window,
   or press ENTER to choose a folder. It remembers the choice.

The game files are recognized by their hashes, not by their names.
- **The GOG release** is the verified one.
- **Steam's PC release** should use the same files, but it has not been checked.
- **The arcade dump** is loaded and converted on the fly. Its 68000 program is a slightly
  different revision from GOG's.

## Features

- **Arcade-exact game:** the same game logic as the original board, verified frame by frame
  against MAME (see *How it was verified*).
- **Modern frontend** with menus in English and Spanish, usable with keyboard, gamepad or mouse:
  - start screen with **Continue** (the game is saved automatically every minute and on quit);
  - pause menu and 9 save-state slots;
  - settings in tabs: Video, Audio, Game, Keyboard, Gamepad and System;
  - every shortcut configurable, with a picture of your keyboard or gamepad.
- **Video:**
  - 4:3 or square pixels, integer scaling and high resolutions;
  - sharp, smooth or raw pixel filters, scanlines and a **CRT effect** (Arcade, TV, Soft,
    curvature);
  - fullscreen, and V-sync that keeps audio in step on 60 Hz and high-refresh monitors.
- **Responsiveness:** **run-ahead** (verified against MAME) and reduced input latency; fast
  forward (Tab); fast boot past the Neo Geo logo; pause when the window loses focus.
- **Controls:**
  - keyboard and gamepad for two players, remappable, with each player's gamepad remembered;
  - gamepad type (Xbox, PlayStation, Nintendo or arcade stick) and four gamepad profiles;
  - autofire per player;
  - gamepad rumble driven by the game's sound effects.
- **Portable mode:** create a `save` folder next to the executable and everything is kept there.
- **Game modes:** Arcade (the whole game, with a chosen number of continues and a results page)
  and **Mission mode** (start at any mission you have reached, straight to the soldier select).
- **Records:** best time and score per mission and the best Arcade score, from clean games only.
- **Boss health bar** for every mission's boss, and **Boss Rush**: the six bosses in a row, with
  your score, lives, bombs and weapon carried from one to the next (the first time, the port
  prepares the fights by playing the missions itself, which takes a few minutes).
- **Slowdown:** optional removal of the original slowdown (68000 at 2x or 4x speed).
- **Red blood:** the game's own hidden BLOOD option.
- **Music and sounds menu:** every track, effect and voice of the game, with names and
  per-sound rumble.
- **The developers' own debug tools:** the hidden developer menu (stage select, sound test,
  debug switches such as invincibility and hit boxes).
- **Cheats:** weapons, bombs, lives, soldier, fat or mummy form, spawning Slugs.
- **Debug panel (F3):** FPS, game logic rate, 68000 load, sprite and palette viewers.

## Controls

| Neo Geo | Player 1 keyboard | Player 2 keyboard | Gamepad |
|---|---|---|---|
| Joystick | Arrow keys | I J K L | D-pad |
| A (shoot) | Z | A | X / Square |
| B (jump) | X | S | A / Cross |
| C (grenade) | C | D | B / Circle |
| D | V | F | Y / Triangle |
| Start | 1 | 2 | Start |
| Coin | 5 | 6 | Back / Select |
| Autofire | B | G | RB / R1 |

| Key (default; all configurable) | Action |
|---|---|
| Esc (gamepad: Guide, or Back + Start) | Menu |
| F5 / F9 | Save / load state |
| F7 / F8 | Previous / next save slot |
| P | Pause |
| Tab | Fast forward (while held) |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot (PNG) |
| F2 | Board test button (service menu: difficulty, lives...) |
| F3 | Debug panel |
| F4 | Developers' menu (with developer mode on, during a mission) |

Settings are saved in `%APPDATA%\ProjectoDecompile\mslugx\mslugx.ini`. Version 2.0 reads the
settings of 1.x, but not its save states (their format changed).

## How it works

1. **Analysis.** The game's 68000 program was mapped with a recursive disassembler and with
   execution traces from MAME.
2. **Recompilation.** Every instruction is translated to C. The translation keeps the 68000's
   exact cycle counts and interrupt timing.
3. **Runtime.** The Neo Geo hardware is a native runtime written for this project:
   - memory map and the cartridge's protection chip;
   - the LSPC video chip (sprites, fix layer, line timing and interrupts);
   - the real-time clock;
   - the Z80 sound CPU and the YM2610 sound chip.

The released executable cannot contain the recompiled C code, because that code is derived
from the game. It runs the game's 68000 program through a cycle-exact interpreter instead, at
about 8 times real time on a modern PC. If you build from source with your own copy, you also
get the recompiled program, which is used when your copy matches the one it was generated from.

## How it was verified

MAME was used as the reference, never as a source of code.
- **The comparison:** the port and MAME run the same inputs from the same state. Every frame's
  image, plus the RAM and palette at every vertical blank, must be identical.
- **Mission 1:** a recorded playthrough of about 12,000 frames.
- **The rest of the game:** scripted runs of 11 entries of the developers' stage select, 9,000
  frames each.

All of these are identical, both with the recompiled program and with the interpreter alone.
One stage-select run still shows a known difference, a rare Z80 synchronization case.

Details are in `docs/PHASE8_STAGES.md` and `docs/PHASE11_SHARED.md`. The runtime and the frontend
are shared with the Metal Slug 3, 4 and 5 ports by the same author, each verified against MAME the
same way.

## Building from source (Windows)

1. Clone the repository into a folder, for example `C:\Projecto Decompile\repo`.
2. Run `repo\setup\INSTALL.bat`. It downloads every tool into `tools\` next to the repository
   from its official source, checks its hash, and needs no administrator rights. The tools are
   CMake, Ninja, llvm-mingw, SDL3, Python, MAME and Ghidra. MAME and Ghidra are only needed for
   the analysis and verification tools.
3. Activate the environment in PowerShell (`. .\env.ps1`) and build:

```
cmake -S repo -B repo\build-dist -G Ninja -DCMAKE_BUILD_TYPE=Release -DPD_RECOMPILED=OFF
cmake --build repo\build-dist
```

That is the distributable build: it needs no game files. A personal build with the recompiled
program needs your copy in `roms\` (`INSTALL.bat` offers to copy it from GOG) and the analysis
listings. `CLAUDE.md` and the `docs/` folder describe every phase of the project.

## Repository layout

| Path | Contents |
|---|---|
| `src/cpu` | dispatcher, interpreter glue, cycle tables |
| `src/neogeo` | hardware runtime and the copy loader (shared with the other Metal Slug ports) |
| `src/port` | the SDL3 frontend (shared) |
| `src/games/mslugx` | Metal Slug X's descriptor (hashes only), profile (RAM addresses) and sound names |
| `tools/` | analysis, recompiler (`recomp68k/`), MAME scripts and comparison tools |
| `tests/` | instruction-level differential tests, whole-program lockstep tests, loader tests |
| `external/` | Musashi, ymfm, the Z80 core, stb_truetype and the menu fonts (`THIRD_PARTY.md`) |
| `docs/` | per-phase documentation |

## License

The port's code is MIT licensed (`LICENSE`). Third-party code keeps its own license
(`THIRD_PARTY.md`): Musashi (MIT), ymfm (BSD-3-Clause), floooh/chips z80.h (zlib), SDL3 (zlib),
stb_truetype (MIT) and the menu fonts Black Ops One and Chakra Petch (SIL Open Font License).
