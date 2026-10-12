# Phase 5: native improvements

The port plays like a regular Windows program: it finds the GOG installation on its own and has
its own menu, save states, image filters, configurable controls for 2 players and an option to
remove the original slowdown. Everything that changes the game's behaviour is optional and off by
default: normal mode is still identical to MAME (Phase 4).

## Usage

`mslugx.exe` with no options. ROMs are searched in this order:

1. `--roms FOLDER`;
2. the folder remembered in `mslugx.ini`;
3. `roms\` next to the executable;
4. the GOG installation: Windows registry `HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\2046360890`
   (or any game registered as "Metal Slug X") and the usual folders on every drive;
5. if nothing works, a folder picker (either the game root or `resources\game` will do).

Files are always validated by SHA-256 (`src/neogeo/roms.c`).

`cmake --install build --prefix FOLDER` leaves `mslugx.exe` and `SDL3.dll` ready to copy. The
executable contains code translated from the user's own copy: it is for personal use, not for
redistribution.

### Fixed keys

| Key | Action |
|---|---|
| Esc (gamepad: Guide, or Back + Start) | menu |
| F5 / F9 | save / load state in the active slot |
| F11 or Alt+Enter | fullscreen |
| F2 | test button: BIOS service menu (difficulty, lives, etc.) |
| F12 | BMP screenshot in the preferences folder |
| P | pause |

Default controls:

- **Player 1:**
  - arrow keys;
  - Z / X / C / V = A (fire) / B (jump) / C (grenade) / D;
  - 1 = Start, 5 = coin.
- **Player 2:**
  - I / K / J / L to move;
  - A / S / D / F = A / B / C / D;
  - 2 = Start, 6 = coin.
- **Gamepads:** the first one connected is player 1 and the second one, player 2. With the Xbox
  layout: X / A / B / Y = A / B / C / D; d-pad or left stick; Start; Back = coin.

### Menu

- Continue.
- Save or load state, in 9 slots (← / → changes the slot).
- Slowdown: "yes (original)", "no (68000 x2)" or "no (68000 x4)".
- Fullscreen.
- Aspect: 4:3, like the monitor, or square pixels.
- Filter: sharp, smooth or pixels; scanlines.
- Volume.
- Controls for each player, with key and button capture.
- Service menu, reset and quit.

It can be navigated with the keyboard or a gamepad.

### User files

They all live in `%APPDATA%\ProjectoDecompile\mslugx\`:

- `mslugx.ini`: settings, format described in `src/port/config.c`;
- `sram.bin`;
- `estado_N.bin`;
- screenshots.

## Image

The Neo Geo draws 320×224 and the monitor stretches it to 4:3, so pixels are not square
(width/height ≈ 0.93). Modes:

- **4:3 aspect** (default): the image fills 4:3 inside the window, with black bars if there is
  space left over.
- **Square pixels:** 320:224. If the filter is also "pixels", it uses an exact integer scale.
- **Sharp filter** (default): first scales up by an integer factor without smoothing (the smallest
  one that covers the final size) and then fits with linear interpolation. All pixels end up the
  same size and edges do not look blurry.
- **Scanlines:** a dark band over the lower half of each original line. Only drawn from 2 screen
  pixels per line upwards.

## Original slowdown and turbo

**Measurement.** The game runs its logic at 30 Hz: the main loop waits for VBlank (PC
`0x39B0`-`0x3A3E`) and exits at `0x3A42` once every 2 frames. When the logic of a cycle does not
finish in time, the cycle lasts 3 or 4 frames (20 or 15 Hz): that is the slowdown seen with lots
of explosions. It is measured with `PD_CONTAR_PC=3A42` (headless).

**Turbo** (`src/cpu/cycles68k.h`, macro `CIC`): every instruction costs 2^k times fewer cycles.
It is a faster 68000 against the same video and sound hardware, like MAME's overclock. With k = 0
nothing changes: the 5 tests, including Mission 1 against MAME, pass the same.

Frames lost to slowdown (intervals of more than 2 frames between loop iterations, in gameplay
stretches):

| Run | Original | Turbo x2 | Turbo x4 |
|---|---|---|---|
| Attract mode demo (12,000 frames) | 10.3 % | 0.2 % | 0.0 % |
| Mission 1 (user recording) | 9.0 % | 0.0 % | — |

Notes on the measurement:

- The attract demo is the same with any turbo.
- With turbo, the Mission 1 recording stops matching the original playthrough, because the game
  advances faster than the recorded inputs, but it still works as a gameplay load.
- Turbo x2 is enough. x4 exists in the ini in case some stretch needs it.

## Save states

`ng_state_save` and `ng_state_load` (`src/neogeo/savestate.c`) save the whole machine between
frames, in about 835 KB:

- 68000 and video and I/O hardware (`NgMachine`, which includes the SRAM);
- scheduler;
- uPD4990A;
- Z80 and sound state;
- YM2610, with ymfm's `save_restore`.

The recompiled code keeps no state of its own between frames: at the end of each frame, all
native calls have unwound.

A state carries a version and a size. If they do not match the executable, it is rejected without
touching the machine.

**Test:** `ctest` → `estado_guardado` (`--probar-estado 600`). Saves a state, runs 600 frames,
resets the machine, loads the state and repeats. RAM, image and audio come out identical. The same
was checked on Mission 1 (900 frames from frame 5000).

## New test tools

- `PD_PRUEBA_VENTANA=N,file.png[,menu]`: after N frames, saves what the window shows (with the
  menu open if requested) and exits.
- `PD_PREF=folder\`: a different preferences folder, so the user's settings are not touched.
- `PD_CONTAR_PC=hex` and `PD_CONTAR_ARCHIVO`: iterations per frame of an address.
- V fingerprints (`--huellas`) include the PC interrupted by VBlank (`int=`).
