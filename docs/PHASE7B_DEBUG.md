# Phase 7B: debug mode and cheats

**State:** closed, pending approval.

Metal Slug X keeps the debug tools its developers used. The port exposes them instead of
rebuilding them, and adds RAM cheats only for what they lack. Nothing here changes the emulation
when it is off: `ctest` (Mission 1 against MAME) still passes.

## What the game hides

**Development mode.** The BIOS keeps a "development hardware" flag at `0x10FE80` (always 0 on a
retail MVS). The game reads it in three places:

- `0x2588`: without it, the game clears its **debug dips** (`0x10F000`-`0x10F001`) every frame;
- `0x15408`: copies it to `0x10C2BE` when a game starts; the pause code checks the copy;
- `0x1214A`: sound handling of the demo.

The port forces the flag (and its copy) **only for the cartridge's reads** (`ng_forced_read`, as
the red blood): the BIOS and its RAM test see the real RAM.

**Developer menu.** With the flag on, during a mission (`0x15448`, MVS branch):

- holding **SELECT** and pressing **D** pauses the game;
- doing it again opens the developers' menu (`0x15750`, items at `0x15812`, Japanese text).

Hidden strings in the P ROM: `MISSION SELECT`, `SOUND TEST`, `SET DEBUG DIP`, `TEST STAGE`,
`TEST1 for ANDY THE WIZARD`... The menu (translated):

| Item | What it does |
|---|---|
| Close the menu (stays paused) | In pause, START advances one frame (development mode) |
| SET DEBUG DIP | 11 switches, see below |
| Stage select | 13 entries (`mission:area`), e.g. 5:1 starts in New York's main street |
| TEST STAGE | 8 test stages, one per developer (ANDY THE WIZARD, MEEHER, YOKO-SHU, NISHINO, NAKATSUKA, TAGUCHI...) |
| Sound test | The developers' own: code, bank, channels |
| COLOR BAR | Color bars for the monitor |

**Debug dips** (verified by toggling each item of SET DEBUG DIP and dumping the RAM, and by
forcing each bit over the Mission 1 recording):

| Item | Bit (word `0x10F000`) | Effect |
|---|---|---|
| Invincible (ムテキ) | `0x8000` | Damage is skipped (`0x3519E`) |
| VIEW INFOMATION | `0x0100` | Progress, free objects, scroll, enemies on screen |
| Pause (イッテキ PAUSE) | `0x0200` | Pause allowed at any time (`0x1553E`) |
| VIEW GEO SCROLL A / B | `0x0800` / `0x1000` | Ground collision lines |
| VIEW ATTACK RECT / BODY RECT | `0x2000` / `0x4000` | Collision boxes |
| Unlimited grenades | `0x0004` | Bombs are not spent (`0xC0810`) |
| Unlimited ammo | `0x0008` | Ammo is not spent (`0xC0738`) |
| VIEW PA POINT | `0x0002` | A reference marker (`0x10E968`) |
| D button: kill all enemies | `0x0001` | `0x4BB4` |
| (not in the menu) | `0x0400` | CPU meter: darkens the screen (shadow) while the 68000 works |

## RAM cheats

Found in the code and confirmed with dumps (addresses in `src/port/profile.h`):

- **Per-player table** `0x10C718` (P1/P2 interleaved): `+6` weapon, `+4` sub-type, `+8` bombs,
  `+0xC` ammo word (`0xFFFF` = infinite).
- **Weapons:** the pickup routine `0xC0928` takes the weapon number (0-15) and reads three
  tables in the ROM (sub-type `0xC08A8`, ammo `0xC08B8`, flag `0xC0918`); the port does the same
  writes reading those tables from the user's ROM, without the announcer voice. Names from the
  shots seen and the voice each weapon plays (`0xC08F8`):

  | # | Weapon | # | Weapon |
  |---|---|---|---|
  | 0 | Pistol | 8 | Big Rocket Launcher |
  | 1 | Big blast (no voice; unused?) | 9 | Big Flame Shot |
  | 2 | Shotgun | 10 | Big Heavy Machine Gun |
  | 3 | Rocket Launcher | 11 | Big Laser Shot |
  | 4 | Flame Shot | 12 | Super Grenade |
  | 5 | Heavy Machine Gun | 13 | Iron Lizard |
  | 6 | Laser Shot | 14 | Drop Shot |
  | 7 | Big Shotgun | 15 | Enemy Chaser |

  7-11 share the voice of 2-6 and fire bigger shots (probably the versions used when the soldier
  is fat).
- **Lives:** `0x101288` (P1) / `0x101338` (P2), initialized from the HERO soft dip (`0xC942C`).
- **Soldier:** status object `+0x83` (select order: Marco, Eri, Tarma, Fio), `+0x82` and player
  object `+0x6F` (internal: Marco 0, Tarma 1, Eri 2, Fio 3). It takes effect when the player
  respawns.
- **Forms** (player object `+0x93` current, `+0x94` requested; transitions table `0xC32E4`,
  handler around `0xC340E`): 0 normal with the pistol, 1 normal with another weapon, 2 fat,
  3 mummy. The game plays its own transformation when a form is requested.
  - Fat: each food adds `0x100` to the player's counter (table `+0x10`, routine `0xC0BF4`) and
    form 2 is requested at `0x780`. While fat, the routine `0xC0BAE` checks the stage
    (`0x10C2F6`): only in stages 4-7, 9 and 10 (or near a food object, `0x10E9F8`) the counter
    runs down slowly; anywhere else the soldier slims down at once. The cheat keeps the counter
    full and makes that routine alone read stage 4 (a forced read limited to its PC range), so
    the soldier stays fat until he dies or the cheat changes the form. Fat soldiers fire the big
    shots.
  - Mummy: form 3 requested directly; back to normal with form 0 (or 1 with a weapon).
- **In a mission:** `0x100088 == 2`.

## In the port

- **Extras > Cheats:** player 1/2, weapon (choose with left/right, give with Enter), 99 bombs,
  lives, soldier, form (normal / fat / mummy), and the invincible / infinite bombs / infinite ammo / D kills all switches.
  Session only: they are off on the next start.
- **Extras > Debug:** developer mode (saved as `devmode=1`), open the developer menu (also
  **F4**: the port presses SELECT + D for the player, who has no SELECT button on a gamepad),
  collision boxes, game info, ground lines, CPU meter, PA point, pause anywhere.
- Both pages are in English and Spanish.
- Code: `src/port/cheats.c`, `cheats.h`; addresses in `profile.h`.

## Tests

- Headless: `PD_DIPS=hex`, `PD_DEVMODE=1` and `PD_CHEAT=frame,kind,player,value[;...]` (kind:
  weapon, bombs, lives, soldier, form, devmenu; poke = player object byte, for research). The script file accepts `select1`.
- Window: `PD_WINDOW_TEST=N,file.png,cheats` or `,debug` for screenshots of the pages.
- Verified: the 16 weapons fire over the Mission 1 recording; lives 9 and 99 bombs show in the
  HUD; Fio after a respawn; fat in Mission 1 from frame 1872 until the cheat turned it off at 2600; mummy and cured; `devmenu` opens the developer menu with no manual input.

## Risks

- Changing the soldier only applies on the next respawn (the sprites are loaded then).
- Weapon 1 and the "big" names are inferred, not documented by SNK.
- With developer mode on, the game behaves as on a development board in small ways (frame step
  with START while paused); it is off by default.
