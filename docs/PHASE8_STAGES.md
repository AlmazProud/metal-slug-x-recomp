# Phase 8: every stage against MAME

**State:** closed, pending approval.

Mission 1 was verified frame by frame in Phase 4 with a recording of the user's game. Phase 8
verifies the rest of the game without anyone playing it: scripted runs reach each entry of the
developers' stage select and play it with a simple bot, in MAME and in the port, and compare
the same fingerprints (RAM and palette on every VBlank, image on every frame).

## Result

| Stage select entry | Frames | VBlanks (RAM + palette) | Images | State |
|---|---|---|---|---|
| Mission 1 (Phase 4, user's recording) | 11,970 | 11,771 identical | 11,968 identical | test `mission1_vs_mame` |
| 2 | 9,000 | 8,799 identical | 3 frames differ (4-6 pixels on one line) | known difference |
| 3, 4, 5, 7, 8, 9, 10, 11, 12, 13 | 9,000 each | all identical | all identical | tests `stageNN_vs_mame` |
| 6 | 9,000 | identical up to VBlank 5070 | | known difference (Z80) |

`ctest` runs 16 tests (`ctest -j 8`: about 20 s).

## Method

- **`tools/stage_run.py --stage N`** writes an input script in the port's headless format:
  coin, START, soldier select, the developers' menu (SELECT held + D twice), stage select entry
  N, then a bot (mostly right and firing; jumps, grenades, some up/left) with the debug dip
  invincibility on so it does not die.
- **`tools/mame/scripted_run.lua`** drives MAME's inputs from that script and reproduces the port's
  debug mode exactly: development mode (`0x10FE80` and its copy `0x10C2BE`) and the dips
  (`0x10F000`) are forced only for reads made by cartridge code (PC < `0xC00000`), through read
  taps. The MVS edge connector has no SELECT in MAME: a tap clears its bit in the 68000's reads.
  It then runs `fingerprints.lua` and `inputs.lua` as for Mission 1.
- **MAME records the run (`-record stageNN.inp`).** The port's clock date comes from the
  recording's header (base time, local time), which is how MAME sets the uPD4990A on playback.
  `--replay` plays the same run back for RAM dumps (`PD_DUMP_V`), cycle traces (`--debug` +
  `PD_EXTRA_SCRIPT=cycles.lua`, `PD_TRACE_REGS=1`) or screenshots.
- References live in `analysis/phase8/` (outside the repo); `tools/verify_mission.py --devmode
  --dips 8000` compares.

Measurement lessons:
- **The clock matters.** The game reads the uPD4990A during play; one second off makes a run
  diverge thousands of frames in. Taking the date when the script starts was off by up to two
  seconds when several MAMEs started together; the `.inp` header is exact.
- **Taps that alter reads also alter Lua's reads.** `fingerprints.lua` flags its own RAM reads
  (`pd_lua_read`) so the forced-read taps skip them; otherwise MAME's fingerprints showed the
  forced values as if they were in RAM.

## Fixed

**Line counter read timing** (`src/neogeo/video.c`). Stages 8 and 12 diverged because the wait loop
(`0x39B0`-`0x3A41`, which adds the line counter `0x3C0006` to a random seed) read it right on a
line boundary. Cycle traces showed the same cycle in both, but MAME read one line less: the
counter is sampled during the operand read, not at the end of the instruction (the port's cycle
counter already includes the final prefetch). Sampling 6 cycles before the end matches MAME in
every run; 4 and 8 do not (stage 9 breaks with 8).

## Known differences

- **Stage 6 (VBlank 5071): 68000 / Z80 synchronization.** The game waits for the Z80's reply in a
  polling loop (`0x1092`: `move.b $320000,d3`). MAME sees the reply one iteration earlier than
  the port: in the port the Z80 is about 1.75 µs behind (reply to command `03` at 88,881,583.0 µs
  against 88,881,581.25 µs in MAME) and, outside the 50 µs fine window after a command, it is only
  advanced at event boundaries. This is the pending "sample-accurate Z80" item: the exact
  MAME scheduler slices for the Z80 are not modeled. It affects the BIOS / game timing only in
  this narrow case; delivering the command at the write time instead of the end of the
  instruction did not change it (the Z80 takes the NMI at its next instruction anyway).
- **Stage 2: 3 images** (frames 2974, 2986, 3049, during a fade) differ by 4-6 pixels on a single
  line, with RAM and palette identical: a VRAM write landing on a line boundary. The write
  ordering threshold from Phase 4 (4 cycles) is still the best value (others break Mission 1).

## Also in this phase

- The user's sound names (40 sounds, mostly voices) are now built-in names
  (`tools/sound_catalog.py`, `src/port/profile_sounds.c`), with Spanish for the descriptive ones;
  announcer lines stay as spoken. Their voices fixed two weapon names of Phase 7B (12 Enemy
  Chaser, 15 Super Grenade), and their rumble settings for effects (grenade explosion, laser...)
  are now defaults.
- Tools: `cycles.lua` can log D0-D7 (`PD_TRACE_REGS`), `compare_cycles.py` ignores them,
  `trace_z80_reply.lua` times the Z80's replies, `PD_WATCH_PC` (port) logs every execution of an
  address.
