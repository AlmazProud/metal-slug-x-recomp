# Phase 3: hardware runtime

The port (`build/src/mslugx.exe`) boots **the user's recompiled MVS BIOS**, passes its tests
and enters the game: attract mode, coin, START, "How to play", soldier select and a playable
Mission 1, **with sound** (Phase 3B). It only needs `SDL3.dll`.

```
mslugx.exe --roms "K:\Metal Slug X\resources\game"
```

## Components

| File | What it models |
|---|---|
| `src/neogeo/roms.c`, `sha256.c` | Loads the GOG/DotEmu files and validates them by SHA-256. The repo only contains the hashes. |
| `src/neogeo/bus.c` | Full 68000 memory map: switchable vectors, P1, RAM, P2 window and banks, ALTERA protection, I/O, system latch, two-bank palette, BIOS and SRAM with write lock. |
| `src/neogeo/video.c` | LSPC: VRAM with read buffer and modulo, registers, auto-animation, per-line sprite list (up to 96 of 381), sprites with shrinking, chaining, flipping and wraparound, fix layer (BIOS or cartridge), color conversion through the resistor network, shadow. |
| `src/neogeo/machine.c` | Cycle-based 68000 scheduler: lines, VBlank (IRQ1), IRQ2 (position counter with its three reload modes), power-on IRQ3, watchdog and interrupt delivery. |
| `src/neogeo/rtc.c` | NEC uPD4990A clock in serial mode: commands, shift register, TP and DATA OUT. |
| `src/neogeo/sound.c` | Z80 (z80.h, zlib) with its map (M1 with NEO-ZMC banks, RAM, latch, NMI) and YM2610 (ymfm, BSD-3, via `ym2610.cpp`), interleaved with the 68000 like the MAME scheduler (Phase 3B). |
| `src/port/main.c` | SDL3: integer-scaled window, keyboard and gamepad, 59.1856 Hz pacing, 55,555 Hz audio (50 ms cushion), persistent SRAM and a headless mode for the tests (`--screenshots`, `--script`, `--wav`, `--trace`, `--trace-io`). |

**BIOS decision:** we use the user's BIOS (`sp-u2`, recompiled together with P1), not a
reimplementation (HLE). It is already verified in the lockstep, MAME (the oracle) runs the
same binary, and an HLE would add behavior of its own with no way to validate it.

## Findings verified against MAME

Tools: `tools/mame/screenshots.lua`, `trace_ports.lua`, `trace_sound.lua`,
`z80_latency.lua` and `trace_io.lua`, plus `tools/compare_screenshots.py`. In the port, the
`--screenshots`, `--trace` and `--trace-io` options produce the same formats.

1. **Idle port values** (`trace_ports.lua`):
   - `300001`: DIP = `FF`;
   - `300081`: TEST = `80` (bit 6 at 0 means MVS);
   - `320001`: `1F`, plus TP in bit 6 and DATA OUT in bit 7;
   - `340000` and `380000`: `FF` in the high byte.
2. **The BIOS waits for TP edges from the clock** (loop at `C11D6A`). Without the uPD4990A,
   Phase 2's minimal model got stuck there.
3. **The BIOS and the game wait for Z80 replies** (in 3A, before the real Z80, a stand-in with
   these latencies was used). Command 1 is answered with 1 and command 3 with 0; the rest do
   not change the reply. If reply 1 never arrives, the BIOS halts with error 8 (`C11FE8`).
   Latencies measured on Z80 port `0x0C`:

   | Command | Latency | In 68000 cycles |
   |---|---|---|
   | 1 | 14,660.5–14,662 µs | 175,929 |
   | 3 | 60.4–61.5 µs | 729 |

   The read of `0x0C` must be intercepted on all its I/O mirrors (`OUT (C)` puts B in the high
   byte of the address).
4. **DotEmu sprite pixel order:** each 8-pixel half is reversed relative to the screen
   (`col ^ 7`). This is consistent with "pixel x = bit 7 − x" in `ROM_MAP.md`. It was spotted
   because "FIRE" looked mirrored by halves.
5. **Frame phase:** in MAME, at time 0 the beam is at the start of VBlank (vpos 240), and the
   first VBlank arrives one full frame later. With the origin at vpos 0, every write synchronized
   with VBlank arrived 24 lines (1536 µs) early. That is why the port's frame ends at VBlank and
   its number matches `screen:frame_number()`.
6. **Interrupts at instruction boundaries:** the generator emits a checkpoint before every
   instruction (`program.py`; disabled with `--no-per-instruction-points`). An interrupt is
   taken before the first instruction that starts at or after its cycle, not at the next
   dispatch point.

7. **MAME frame numbering:** the screenshot MAME labels as frame N is the image completed while
   running frame N (in the port, when `ng.frames` goes from N to N + 1). With the previous
   numbering, off by one, only 321 of 451 consecutive frames matched; with this one, 448 match.
   The input script uses the same numbering.

## Phase 3B: sound

- **Real Z80 + YM2610** instead of the stand-in, with the same YM core as MAME (ymfm, medium
  fidelity, 55,555 Hz) and its mix (SSG ×0.84; FM left/right ×0.98).
- **Interleaving like MAME:**
  - the Z80 runs up to each 68000 slice boundary: line start, VBlank, IRQ2 reload, YM timers;
  - each command synchronizes immediately and opens 50 µs of fine interleaving (perfect
    quantum);
  - outside that window, the 68000 sees the reply in the next slice.
- **Timeline against MAME** (`--trace` vs `z80_latency.lua`): commands, Z80 replies and the
  moment the 68000 sees them match within a few microseconds.

  | Event | MAME | Port |
  |---|---|---|
  | Reply 01 seen by the 68000 | 4,684,418.9 µs | 4,684,418.8 µs |
  | Z80 reply 00 after CMD 03 | 4,736,282.0 µs | 4,736,282.0 µs |
  | E1 at frame 879 | 14,868,559.1 µs | 14,868,558.5 µs |

  This holds for the first 18 commands (up to frame 880).
- **Audio against MAME's `-wavwrite`** (`tools/compare_audio.py`, MAME with
  `-samplerate 55555`):
  - in the stretches with the same command sequence, the correlation is **1.00000** with a
    fixed offset of 6 samples (MAME's stream latency);
  - from frame ~919 on, the game triggers sounds one frame earlier than MAME (990-sample
    offset): this is the 68000 logic drift described below, not a sound issue.
- **Windowed:** average queue of 32 ms, no dropouts or discards over 20 s (50 ms initial
  cushion).
- **Performance:** about 1600 frames/s headless (27 times real time); windowed, ~12 % of one
  core.

## Results

- **Colors and rendering, pixel by pixel against MAME:**
  - of 451 consecutive frames (250–700), **448 are identical**; the other 3 differ in
    1-18 pixels (logo sparkles);
  - at frames 100, 200, 300, 400, 500, 600, 900, 1200, 1500 and 1800, 8 of 10 are identical.
    The attract-mode demo diverges before 1500 (cycle drift, below).
- **Timing:**
  - the first command to the Z80 (4.67 s after power-on) lands within 1.25 µs of MAME's;
  - in the window of frames 280–282, all 4436 I/O writes match in order, address and data,
    with a constant offset of 1.1 µs.
- **Simulated game** (`--script`): coin, START, "How to play", soldier select and a playable
  Mission 1, with shooting, scoring and enemies.
- **Performance:** without sound (3A), about 3500 frames/s; with the Z80 and the YM2610 (3B),
  about 1600 frames/s headless. Windowed it uses about 12 % of one core.
- **Accesses:** 0 unmapped reads and 0 unmapped writes; the watchdog never resets the machine.
- **Tests (`ctest`): 3/3.**
  - Both lockstep tests now also place a checkpoint every ~1000 cycles: 20,097 checkpoints
    from reset and 304,458 from the game.

## Known differences from MAME (for Phase 4)

> **Resolved in Phase 4** (`docs/PHASE4_VERIFICATION.md`): the port reproduces Mission 1 with no
> RAM or image divergences. The list is kept as a record of what was found.

1. **Order of 32-bit writes.** With `MOVE.L` to `-(An)`, the 68000 (and MAME) writes the low
   word first; the port always writes the high word first. The final value is the same, but
   the order matters for I/O registers. It was observed in palette writes.
2. **68000 cycle drift.**
   - About 0.5 µs per frame: per-instruction timings come from Musashi's table, while MAME
     uses its cycle-exact 68000 core.
   - 32-bit writes are logged on the same cycle; on hardware they are 4 cycles apart.
3. **Visible effect.** Around frame 900 of the attract mode, the game does something one frame
   earlier than MAME (command E5 at frame 919, and then the demo). This is the next divergence
   to track down in Phase 4 with per-frame RAM comparison.
4. **Clock.** The BIOS shows the time; the port uses the system time or `--date`, and MAME the
   system time.
5. **Unmapped reads.** They return `0xFFFF`; on real hardware they return the last value on
   the bus (the prefetch). None were observed.
