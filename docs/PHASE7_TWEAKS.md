# Phase 7A: V-sync and red blood

**State:** closed.

Both are menu options and neither changes the emulation: `ctest` (with Mission 1 against MAME)
still passes.

## Red blood

**Finding:** white blood does not depend on the region but on an option of the game itself. The
P ROM header declares the options the BIOS shows in the service menu ("soft dips"). The US and
Europe table contains:

- HERO (lives) and CONTINUE;
- DIFFICULTY (LEVEL 1-8) and PLAY TIME (60-90);
- DEMO SOUND, PLAY MANUAL and **BLOOD (OFF/ON)**;
- CREDIT/LEVEL.

In the US, BLOOD defaults to OFF.

Notes:

- The GOG file stores words with their bytes swapped ("EN-OEG"). `roms.c` already restores them
  to the 68000's view.
- The BIOS copies the options to `0x10FD84`: two times, two counts and ten options from
  `0x10FD8A`, so BLOOD ends up at `0x10FD8F`.
- The game checks it at `0x1B2BE` (`tst.b $10FD8F`).
- Other leads were also ruled out:
  - `0x100003` (decided at `0x2382`) keeps separate credits per player, as in the US;
  - `0x100000` and `0x100001` are the language and the country.

**"Blood: Original / Red" option** (Settings → Game, or `sangre=1` in the ini):

- Writing that byte from power-on broke the BIOS RAM test ("WORK RAM ERROR 10FD8E").
- That is why **only reads by the game's code** are forced (`ng_forced_read` on the bus:
  PC < `0xC00000`).
- The BIOS sees the real RAM and its service menu still shows the user's setting.
- It does not touch the SRAM either. Enabling BLOOD in the service menu still works the same.

**Verification:** with the Mission 1 recording, the first image difference is at frame 1459, the
first enemy that dies. The screenshots show the original's white splatter and the red one with the
option. `--sangre` in headless mode lets you repeat it.

## V-sync

Option in Settings → Video (`vsync=1` by default). There are three possible pacings:

| Situation | Pacing | Audio |
|---|---|---|
| No V-sync | One frame every 16.896 ms (59.1856 Hz, the board) | As before |
| V-sync with a 58 to 62 Hz monitor | One frame per refresh: the game runs at the monitor's rate (1.4 % faster at 60 Hz), like MAME's `-syncrefresh` | Played back at the same ratio (`SDL_SetAudioStreamFrequencyRatio`), with a correction of up to ±0.5 % based on the queue, to keep it at ~50 ms |
| V-sync at other rates (75, 120, 144 Hz...) | Presents on every refresh and an accumulator decides how many frames to emulate at the board's exact rate | Same |

**Measurement** (20 real seconds, `PD_SALIR_TRAS` + `--traza`, 60 Hz monitor):

| Mode | Emulated frames/s | Mean audio queue | Queue empty | Drops |
|---|---|---|---|---|
| V-sync (60 Hz) | 59.966 | 40.6 ms | 0 | 0 |
| No V-sync | 59.184 | 39.3 ms | 0 | 0 |
| V-sync, simulated 144 Hz (`PD_HZ=144`) | 59.098 | 41.5 ms | 0 | 0 |
