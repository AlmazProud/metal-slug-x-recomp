# Phase 6: "Remaster"

Optional improvements on top of the Phase 5 port. All of them can be turned off from the menu,
and none changes the emulation when off: `ctest` (with Mission 1 against MAME) still passes.

| Subphase | Contents | State |
|---|---|---|
| 6A | Autofire and debug mode | Closed (2026-10-12) |
| 6B | Sound catalog, music, voices and effects menu; gamepad rumble | Closed (2026-10-12) |
| 6C | Modern start menu with the game's font | Closed (2026-10-12) |
| 6D | Widescreen | Dropped by the user: it takes the essence away from the game |

## 6A: autofire

While held, button A (fire) presses and releases itself. It is configured per player in the menu
or in `mslugx.ini`:

- `autodisparo1` / `autodisparo2`:
  - `boton`, the default: with a dedicated button, the "Autofire" action in the controls; keys B
    (player 1) and G (player 2), and RB on the gamepad;
  - `disparo`: the fire button is always automatic;
  - `no`.
- `autodisparo_por_segundo`: 10, 7.5 or 5.

**Measurement** (`--guion` with the pattern, counting sound command `F7`, the pistol shot, over
5 s of gameplay):

| Pattern | Shots |
|---|---|
| Button held | 1 |
| Every 4 frames | 50 |
| Every 6 frames | 50 |
| Every 8 frames | 37 |
| Every 12 frames | 25 |

The game does not fire the pistol more than 10 times per second, so the shortest useful period is
6 frames. Automatic weapons (the heavy machine gun) already fire on their own when the button is
held.

## 6A: debug mode

F3, or "Debug mode" in the menu. Panel at the top left:

- **FPS** of the host, emulated frame and turbo.
- **Game logic in Hz:** when leaving the wait, the game stores at `0x10008A` (`-$7F76(a5)`,
  instruction at `0x3A42`) how many VBlanks the logic cycle lasted.
  - 2 = 30 Hz, the normal case;
  - 3 or 4 = 20 or 15 Hz, the slowdown (in red).

  It was verified with Mission 1: the per-VBlank distribution matches the Phase 5 measurement
  (`PD_CONTAR_PC`).
- **68000 load** (half-second average and peak): fraction of line starts at which the 68000 was
  not in the wait loop (`0x39B0`-`0x3A41`). The average is used because the logic runs every 2
  frames and the load of a single frame alternates.
- **Frames lost** to slowdown over the last 10 s.
- **Graph** of the per-frame load over the last 4 s, with frames of a slow cycle in red.

Debug mode keys:

| Key | Action |
|---|---|
| F6 | advance one frame (pauses the game; P resumes) |
| Ctrl+1 / Ctrl+2 | hide the sprites / the fix layer (HUD and text) |
| Ctrl+3 | boxes of the sprites drawn in the frame, without the full-height background columns |
| Ctrl+4 | active palette (4096 colours, 4 palettes per row) |
| Ctrl+5 | work RAM in hexadecimal (PgUp / PgDn / Home) |

**Implementation:**

- `NgDebug ng_dbg` (`neogeo.h`): measurements outside the machine state. They are only computed
  with `ng_dbg.active`.
- Metal Slug X specific addresses are in `src/port/profile.h`, a first step towards a game
  profile.

**Tests:**

- `PD_DEBUG_VISTAS=cajas,paleta,memoria` opens the views when the mode is enabled.
- With `PD_PRUEBA_VENTANA`, the window runs without waiting for the clock.

## 6B: the Metal Slug X sound driver

The 68000 controls sound with one-byte commands to the Z80 (`ng_sound_command`). It was worked out
from the Mission 1 command trace (`--traza`) and from catalogs made without the game running:

- **Modes:** commands `00`, `10` and `11` set a mode that stays active, and the next command is
  interpreted in that mode. The same number sounds different in each one.
  - **`00`, music:** tracks `20`-`31`, 18 in total.
  - **`10`, effects:** `20`-`FF`, 185 with sound.
  - **`11`, voices:** `20`-`6E`, 61 with sound; includes some digitised effects.
- **System:**
  - `03` resets the driver (the game sends `03 03 06` before each music track);
  - `01` is the BIOS slot switch (leaves the driver waiting for a `03`);
  - `0C` shows up before some effects;
  - `02` and `0F` sound in any mode.
- **Out of range:** the driver reads past its tables and plays garbage.

**Catalog** (`--catalogo-sonido` with `PD_CATALOGO_PREFIJO=MM`, from frame 1300, right after the
game resets the driver):

- for each command: duration, whether it loops, peak and notes started per channel type
  (`ng_notes`: FM, SSG, ADPCM-A, ADPCM-B);
- the real music tracks use FM and ADPCM at the same time and loop.

`tools/sound_catalog.py` uses that to generate `src/port/profile_sounds.c`: command, duration and
name, with no audio data.

**Verification:** from the middle of Mission 1, the sequence `03 03 06` + mode + command gives the
same durations as the clean catalog, in all three modes.

### Names

Only the sounds confirmed with screenshots of the moment they play in the recorded playthrough
were named; the rest are identified by number:

- **Music:**
  - `20` mission complete fanfare;
  - `21` Mission 1;
  - `22` Mission 2 (the one that plays after completing Mission 1);
  - `2C` tank fight;
  - `2E` "How to play";
  - `30` soldier select.
- **Voices:**
  - `31` "Mission 1... start";
  - `37` "Mission complete";
  - `49` soldier select;
  - `50` "Great!" (prisoners rescued).
- **Effects:**
  - `F7` pistol: 50 shots in 5 s with autofire;
  - `8C` Slug vulcan;
  - `84` Slug cannon;
  - `22`, `25`, `21`, `A3` and `F5` explosions;
  - `F2` armour hit.

The attract mode demo has no music (the MVS leaves it silent), so more names need recordings of
other missions.

## 6B: music, effects and voices menu

Menu → "Music and sounds":

- three tabs (← / →, or LB / RB): Music, Effects and Voices;
- Enter / A plays, Del / Y stops, Esc / B returns to the game.

**How it works** (`src/port/sounds.c`):

- on opening, the full game state is saved and the 68000 is halted;
- the Z80 and the YM2610 advance on their own (`ng_sound_only_*`);
- each sound is played the way the game does it: `03 03 06`, the mode and the command, one frame
  apart;
- on closing, the state is restored and the game continues exactly where it was.

**Test:** `ctest` → `menu_sonido` (`--probar-sonidos`).

- Music and voice play: mean energy of 2473 and 620, versus 66 in silence.
- On closing, 300 frames of the game are identical to never having opened the menu (RAM, image
  and audio).

## 6B: rumble

`ng_on_sound_command` notifies the port of each command. The port tracks the driver mode and, for
the identified effects, requests gamepad rumble:

| Effect | Rumble |
|---|---|
| Big explosion | Strong, 380 ms |
| Hit or explosion | Strong to medium, 200-300 ms |
| Slug cannon | Medium, 140 ms |
| Vulcan, burst and pistol | Light, 35-45 ms |

- Within a frame the strongest one wins, and a strong rumble in progress is not cut off by a light
  one.
- Both gamepads rumble, because the game does not say which player each sound belongs to.
- Intensity: "Rumble" menu, or `vibracion=0-100` in `mslugx.ini` (70 by default; 0 turns it off).

## 6C: start screen and menus

**Font** (`src/port/font.c`): the game's own shaded 8x8 font, read from the user's copy.

- It is in the cartridge's fix layer, tiles `0x300` + ASCII code (the one used for "CREDIT").
- Indices: 1 outline, 2-5 gradient, 7 shadow and 15 highlight. The palette was deduced by
  comparing a screenshot with the tiles (`CREDIT 0`).
- The colours are the port's: silver, gold, grey and green.
- It is drawn in upper case, because its lower case letters are very short and hard to tell
  apart.
- At `0x20` + ASCII there is another, flat font, unused.

**Menus** (`src/port/menu.c`): stacked pages, over the dimmed game. The scale is an integer one,
with a canvas of at least 440 x 250.

- **Start**, when the game opens (turned off with `start_screen=0`): Play, Load game, Settings,
  Music and sounds, Extras, Quit.
  - The demo keeps running in the background, without receiving inputs.
  - "Play" inserts a coin and presses START like a player. Verified: CREDIT 1, and then the game
    starts ("How to play", music `2E`).
- **Pause** (Esc, or Guide / Back + Start): Continue, Save and Load game (9 slots), Settings,
  Music and sounds, Extras, Back to start (resets the machine keeping the SRAM), Quit.
- **Settings:**
  - Video: fullscreen, aspect, filter, scanlines;
  - Audio: volume, rumble;
  - Game: slowdown, P1/P2 autofire and rate, start screen;
  - Controls: players 1 and 2.
- **Extras:** debug mode, BIOS service menu, reset the machine.
- Below the panel, a help line explains the selected option.

**Sound menu**, now with the same font:

- **"Recent" tab:** the last 16 sounds the game requested. Useful for identifying one: use the
  weapon and open the menu.
- **F2 or R:** rename the selected sound (with the keyboard).
- **V, or X on the gamepad:** its rumble. Cycle: auto → off → light → medium → strong. "Auto" is
  the one from the table of identified effects.
- It is saved in `%APPDATA%\ProjectoDecompile\mslugx\sonidos.ini`. The key is the mode and command
  in hexadecimal, for example `10F7=Pistola`.
- Each row shows the driver key (for example `10:F7`), so names found by the user can be moved
  into the repo's profile.

**Start screen logo** (`src/port/logo.c`): the game's own.

- On the attract title screen (frame ~1150 with empty SRAM), the logo is sprites 41-124 plus 380
  ("TM"); the red background is 21-40.
- The first time, the port saves the state, runs the machine silently until the "TM" appears
  (first tile `0x309CA`), redraws only those sprites (`ng_video_capture_sprites`) and restores the
  state. It takes less than a second.
- The logo is kept in `logo_titulo.png` in the user folder, never in the repo.

**Interface scale:** continuous, as a 480 x 300 canvas fitted to the window (`font_factor`), with
`SDL_SCALEMODE_PIXELART` so pixels look even at non-integer scales. That way the menus fit any
window size.

**Fixed:** renaming the same sound twice hung the port. `SDL_strdup` allocates with the SDL3.dll
allocator and the name was freed with `free()` from a different C runtime. The port now makes its
own copies. Also, the "R" that opens editing no longer leaks into the name.

**Tests:**

- `PD_PRUEBA_VENTANA=N,png[,menu|sonidos|jugar|renombrar]` for screenshots of each screen;
  `renombrar` renames an effect twice;
- `ctest` 6/6.
