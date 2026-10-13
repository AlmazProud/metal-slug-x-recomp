# metal-slug-x-recomp

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
- **Modern frontend** with menus in English and Spanish:
  - start screen;
  - pause menu;
  - 9 save-state slots;
  - settings for video, audio, game and controls.
- **Video:**
  - 4:3 or square pixels;
  - sharp, smooth or raw pixel filters;
  - scanlines and fullscreen;
  - V-sync that keeps audio in step on 60 Hz and high-refresh monitors.
- **Controls:**
  - keyboard and gamepad for two players, remappable;
  - autofire per player;
  - gamepad rumble driven by the game's sound effects.
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

| Key | Action |
|---|---|
| Esc (gamepad: Guide, or Back + Start) | Menu |
| F5 / F9 | Save / load state |
| P | Pause |
| F11 or Alt+Enter | Fullscreen |
| F12 | Screenshot |
| F2 | Board test button (service menu: difficulty, lives...) |
| F3 | Debug panel |
| F4 | Developers' menu (with developer mode on, during a mission) |

Settings are saved in `%APPDATA%\ProjectoDecompile\mslugx\mslugx.ini`.

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
- **The rest of the game:** scripted runs of 10 entries of the developers' stage select, 9,000
  frames each.

All of these are identical, both with the recompiled program and with the interpreter alone.
Two stage-select runs still show small known differences:
- a rare Z80 synchronization case;
- one line of a fade on three frames.

Details are in `docs/PHASE8_STAGES.md`.

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
| `src/neogeo` | hardware runtime and the copy loader |
| `src/port` | the SDL3 frontend |
| `tools/` | analysis, recompiler (`recomp68k/`), MAME scripts and comparison tools |
| `tests/` | instruction-level differential tests and whole-program lockstep tests |
| `external/` | Musashi, ymfm and the Z80 core (`THIRD_PARTY.md`) |
| `docs/` | per-phase documentation |

## License

The port's code is MIT licensed (`LICENSE`). Third-party code keeps its own license
(`THIRD_PARTY.md`): Musashi (MIT), ymfm (BSD-3-Clause), floooh/chips z80.h (zlib) and SDL3
(zlib).
