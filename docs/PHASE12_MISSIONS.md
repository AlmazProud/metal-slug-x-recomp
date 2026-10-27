# Phase 12: Metal Slug X's missions, score and records

**State:** closed, pending approval.

Phase 11 put Metal Slug X on the shared frontend. This phase gives its profile the addresses
that Mission mode, the Arcade results, Records, the Continue panel and the "Start mission"
cheat need. Each address was found in the port, read in the listing, and checked against MAME.

## The addresses

| What | Address | How it was found |
|---|---|---|
| Mission (0-5) | `0x10C2F5` (word `0x10C2F4`) | RAM of the 13 stage-select runs: 0-5 for entries 1-6 and again for 7-12. Cleared by a new game (`0x16B9E`); the stage select sets it to the stage set index mod 6 (`0x17F44`); the "next mission" step adds 1 (`0x1891E`, 6 after the final mission: the ending) |
| Stage being played | `0x10C2F6` (word) | written at each area's load (`0x18434`, `0x18412`): Mission 1 is stage 0, Mission 2 stages 1-3, Mission 3 from 4, Mission 4 from 5, Mission 5 from 6, the final mission from 9; `0x2C` the ending |
| Stage set and section to start at | `0x10C2FC` / `0x10C2FE` (words) | a new game sets 0 / 0 (`0x17F2A`); a mission load copies them to `0x10C2F8` / `0x10C2FA` (`0x18292`) |
| Player 1's score | `0x10C46A`, 4 BCD bytes | RAM over the runs, checked against the HUD (19200 = `00 01 92 00`; 149600) |
| PLAY MANUAL | high byte of `0x10FD8E` | the BIOS soft dip option 5, read by a new game at `0x16BD4` |

**The developers' stage select** has 13 entries.
- Entries 1-6 are the starts of Missions 1-6 ("Mission N... start!" plays for each).
- Entries 7-12 are Missions 1-6 again, in other stages: jungle, dawn, valley, iron fortress, city,
  sea.
- Entry 13 is the ending.
- The menu's names are in katakana and hiragana (`1:1` desert, `2:1` ruins entrance, `3:1` train,
  `4:1` Hong Kong shopping street, `5:1` New York main street...).

**Also in the program:** the strings `MISSION SELECT`, `PINPOINT ATTACK`, `RESULT` and
`NEW RECORD`, with per-mission times (`0x1584C`-`0x1595F`). This is a time-attack mode of the game
itself, not studied here.

## Starting at a mission

The game's start byte is the stage set index (`0x10C2FD`), which only the stage select turns into
a mission number. A new game clears both in the frame where the player starts, and the mission
loads about 800 frames later.

Writing only `0x10C2FD` loads the right stage, but the mission number stays 0. "Mission 1...
start!" then plays, and the next mission would load the same one again. So both bytes are written:
- **Shared code:** a new profile field, `start_mission_also`, is a second byte that receives the
  start mission (`cheats.c`, `write_start`). It is 0 in the other games, which are unchanged.
- **Metal Slug X's profile:**
  - `start_mission` = `0x10C2FD`, `start_mission_also` = `0x10C2F5`;
  - `missions` = 6, `start_no_section` = 1 (the byte after the start byte is not a section);
  - `mission_now` = `0x10C2F5`, `score_p1` = `0x10C46A`;
  - `manual_word` = `0x10FD8E`, mask `0xFF00`.
- **Profile windows:** four more cover the new game, the stage set routine, the mission load and
  the next mission step. The hash was updated.

## Verification against MAME

References in `analysis/phase12`, recorded with `tools/mame_run.py --game mslugx`:

| Run | What | Port against MAME |
|---|---|---|
| `missions/mission2`-`mission6` | the scripted game started at Missions 2-6: `PD_POKE` of `0x10C2FD` and `0x10C2F5` at frame 704, right after the new game clears them | identical (RAM, palette, every image) |
| `missions/mission3` + `PD_START_MISSION=3` | the "Start mission" option, which writes the same bytes in the same frame | identical |
| `complete/mission1` | the bot (invincible, "D kills all", D pressed often) finishes Mission 1 at frame 6553 | identical |

A first attempt poked at frame 701. The recorded game's new game happens at frame 704 and cleared
the bytes again, so that run was Mission 1. The option, which writes once the new game has cleared
them, was right; the references were re-recorded at 704.

## In the port

- **Mission mode:**
  - the direct start: the coin right after the fast boot, PLAY MANUAL read as off, Mission 3
    written at frame 357, and "Mission 3... start!" at frame 946 with no "How to play";
  - in the window it goes straight to the soldier select, labelled "Mission 3".
- **Progress and records** (the bot's run):
  - "mission 2 unlocked at frame 6553";
  - "mission 1 completed at frame 6553: 5849 frames (1:38.82), score 149600 (not clean)", since
    the debug dips are on.
- **Window checks** (fresh `PD_PREF`):
  - Game modes: Arcade with continues and its record; Mission, 3 / 6 unlocked;
  - the Mission mode page (completed, available, locked; best score and time);
  - Records;
  - the cheats page with "Start mission".

## Tests

`ctest`: 33/33 in the personal build, 31/31 in the interpreter-only build. The new tests are:
- `mslugx_missions_mission2_vs_mame` to `mslugx_missions_mission6_vs_mame`;
- `mslugx_complete_mission1_vs_mame` (the bot's completed Mission 1);
- `mslugx_start_mission_option_vs_mame`;
- `mslugx_mission_mode_direct`;
- `mslugx_records_bot`.

## Not yet

- **A clean recorded mission:** a game played by hand without cheats would test a recorded
  mission (as the Metal Slug port's `partida1` does). With `grabar_partida.bat` it also gives
  Phase 13 its bosses.
- **The game's own time-attack mode** (`PINPOINT ATTACK`) was not studied.
