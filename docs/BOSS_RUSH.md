# Phase 13: Metal Slug X's bosses, health bar and Boss Rush

**State:** closed, pending approval.

The shared frontend's boss health bar and Boss Rush needed Metal Slug X's object layout and its
bosses. Everything was found and checked with bots (the user's choice: no hand-recorded game).

## Objects

| What | Value | Where from |
|---|---|---|
| Object list | circular, root `0x1010A4` | the allocation `0x40BC`: `+4` previous, `+8` next, `+0xC` parent, `+0x10` depth, `+0` the routine |
| Life | word `+0x4E` | `0xA120C` (`sub.w $80(a6),$4e(a6)`) and the fights below |
| Dead | bit 7 of `+0x5F` | set when the life goes below 0 (`0xA121C`) and by the kill check |
| Hit record | `+0x50` | read by each enemy's hit processing (`0x4C24`) |

**The game's kill-everything request** is `0x1067E3` (`-$181D(a5)`).
- `0x4BFE` sets it. Each frame `0x46D4` copies it to `0x1067E2` and clears it.
- Each enemy's hit check (`0x4BAC`, called from `0x4C24`, 262 callers) then gives itself the death
  mark: life 0, dead bit, state 6 at `+0x5A`.
- The debug "D kills all" switch takes the same path.
- Marking objects directly (the Metal Slug port's method) breaks Metal Slug X: `+0x4E` is not a
  life in every object, and the game corrupted and reset. The request is the game's own way.

**Shared code** (other games unchanged; their profiles leave the new fields at 0):
- `kill_flag` (profile): the fast-forward (`ffwd_kill`) sets it instead of marking objects.
  - The boss's object being there counts as "present", since it may die in the frame it appears;
    the state kept is from before.
  - Boss Rush's test defeat sets it too, plus the mark.
- `start_section_at` (profile) and `ProfileRushState.section`: the fast-forward can start a mission
  at a section. Metal Slug X's section is the low byte `0x10C2FF` of the word `0x10C2FE`.
- Headless and `tools/mame/scripted_run.lua`:
  - `PD_KILL_FLAG=address,from[,until]` writes `0xFF` there at every VBlank acknowledge, in both.
  - `PD_TRACE_LIFE=min` logs objects reaching a life (research).
  - `PD_SECTION_ONCE` (research).
- `tools/mame_run.py` keeps `PD_KILL_FLAG` in the run's `_env.txt`.

## The bosses

Each boss was fought from a state saved shortly before it, without the kill request: the objects
with a hit record whose life goes down are the boss.

| Mission | Code (routines) | Life | Notes |
|---|---|---|---|
| 1 | `0x0D5A92`-`0x0D67E6` | 5000 (body), 700 (cannon) | a tank; body and cannon both take damage |
| 2 | `0x026354`-`0x026FCE` | 3500 | to its death routines (`0x026CB0`-`0x026FCE`), which it reaches at once under the kill request |
| 3 | `0x04A380`-`0x04ACDC` | 5500 | to its death routines (`0x04AA3A`-`0x04ACDC`) |
| 4 | `0x0235E8`-`0x0241CC` | 500 each | seven guns, children of the main object (`0x022872`) |
| 5 | `0x0A0C9A`-`0x0A0EE2` | 5000 | the kill request does not kill it; shots do |
| 6 | `0x0415BA`-`0x0417FE` | 6500 | the Martians' mothership |

- **Middle bosses:** one was seen in Mission 5 (`0x0D3E22`, 1400). It is not in the profile.
- **Names** (a web search with the user's permission, from the Metal Slug Wiki and NamuWiki): Iron
  Nokana MK II, Aeshi Nero, Dragon Nosuke, Big Shiee, Hozmi and Rugname. The bar shows them in
  capitals, as the other ports do.
- **`boss_min_life`** is 300, because Mission 4's guns have 500.
- Six profile windows cover the bosses' code, and one covers the list's root.

## Boss Rush: saved states

Metal Slug X has stage sections (Phase 12), but they start a mission's areas, not its boss fights.
So Boss Rush loads states, as the Metal Slug port does.
- **How the states are made:** the fast-forward (`fastfwd.c`) plays each mission from Mission mode's
  direct start with:
  - the kill request every frame;
  - lives, bombs and the Heavy Machine Gun given back every 600 frames;
  - its seeded bot (60 % right, 15 % left, 40 % jumps, 10 % grenades).
- **What is kept:** a state at least 120 frames before the boss's object appears.
- **Mission 2 starts at its last section (12),** because no bot gets through the pyramid.
- **When:** the six states are made the first time Boss Rush runs, in about 84 s (recompiled), and
  kept in the preferences folder.
- **A boss is behind** when its mission goes on to the next one (all six are a mission's last).
- **Score, lives, bombs, weapon and ammo** are carried from boss to boss.

## Verification

- **Against MAME** (`analysis/phase13`, `tools/mame_run.py`):
  - `bosses/mission1`-`6`: each mission from its start to its end, with the seeded bot, lives
    given back and the kill request (`PD_KILL_FLAG`);
  - `bars/mission1`-`6`: the same up to each boss, then a fight without it (bombs given back).
  - All 12 are identical in RAM, palette and every image: about 254,000 frames of play,
    the whole game.
- **Boss bar** (`mslugx_bossbar_mission1`-`5`):
  - Mission 1's tank from 4990/5000 to its defeat;
  - the bosses of Missions 2, 3 and 5 followed down;
  - Mission 4's guns.
  - It was also checked in the window.
- **Boss Rush:**
  - `mslugx_bossrush_full`: the six states made from an empty folder, then the run, with the score
    carried and "cleared, 6/6";
  - `mslugx_bossrush_carry`: from the fourth boss, the Heavy Machine Gun and 25 bombs reach the
    fifth.
- **`ctest`:** 53/53 in the personal build (209 s), 51/51 interpreter-only (448 s).

## Not yet

- Mission 5's middle boss.
- Mission 6's boss bar is not in a test (the bar runs' bot does not reach its fight in the MAME
  recording); Boss Rush covers that boss.
