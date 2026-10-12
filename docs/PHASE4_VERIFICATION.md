# Phase 4: boot and differential verification

**Result:** the user's recorded game (`mision1.inp`, 11,969 frames, from the title screen to
the Sphinx in Mission 1) replays in the port **with no divergence at all** from MAME 0.289:

| What is compared | Where | Result |
|---|---|---|
| Work RAM (64 KB) and visible palette | at every VBlank acknowledge | **11,771 / 11,771 identical** |
| Image (320×224, pixel by pixel) | every frame | **11,968 / 11,968 identical** |
| 68000 sound commands | 1902 commands | same values, within 1 µs |
| Attract mode without inputs (Phase 3) | 451 consecutive frames + 10 scattered | all identical |

The test is automatic: `ctest` → `mission1_vs_mame` (about 11 s). The MAME references and
the recorded inputs live in `analysis/phase4/`, outside the repo.

## Method

1. **Fingerprints in MAME** (`tools/mame/fingerprints.lua`):
   - on every entry to the VBlank handler (write to `3C000C` with bit 2), an FNV hash of RAM
     and the palette;
   - on every frame, a hash of the image (`screen:pixels()`, which returns the previous
     frame's image);
   - inputs, as **changes over time** (`tools/mame/inputs.lua`): the time of the last read with
     the old value and of the first read with the new one.
2. **The port** (`--inputs`, `--fingerprints`, `--date`) applies each input change at that time,
   with the clock date of the recording, and writes the same fingerprints.
3. **To locate each divergence:**
   - `tools/mame/cycles.lua` traces the cycle counter of every instruction in MAME (debugger);
   - the port does the same with `PD_TRACE_INSTR` (everything through the interpreter, same
     cycles);
   - `tools/compare_cycles.py` aligns both traces and points out the first instruction with a
     different duration or flow.

Measurement lessons learned:
- RAM at the frame boundary is useless for comparison: MAME may stop in the middle of an
  instruction. It is measured on entry to the VBlank handler.
- `frame_number()` inside a tap does not advance with the frame; that is why inputs are
  time-based.
- MAME saves its nvram on exit: each run uses a new, empty folder.

## Divergences found and fixed (in order)

1. **Reset:** in MAME, the first instruction starts at cycle 34.
2. **Interrupt entry:**
   - the autovectored acknowledge waits for the E clock (10-cycle period);
   - duration = 10 + wait + 35, i.e. 48 to 58 cycles, not Musashi's 44.
3. **Interrupt sampling** (`src/cpu/cycles68k.h`).
   - **Rule:** an IRQ asserted at T is serviced at the end of an instruction if T ≤ L, where L
     is the last point at which MAME can stop the CPU before the step in which that instruction
     samples interrupts. That point is the end of a bus access or, if there is none, the start
     of the instruction.
   - **Origin of the rules:** they are per-family rules, written from the microcode
     description that MAME documents (`m68000-sdf.cpp`).
   - **Verification:** they were checked opcode by opcode with `tools/sampling_queues.py` and
     `verify_sampling.py`, on 2091 cases with 0 differences.
4. **Instruction cycles that differ from Musashi's table**, measured in MAME:

   | Instruction | Musashi | Real 68000 (MAME) |
   |---|---|---|
   | `ADD/SUB/AND/OR.L <ea>,Dn` and `ADDA/SUBA.L` with Dn/An source | 6 | 8 |
   | `ADDQ.W #,An` | 4 | 8 |
   | `ADDA/SUBA.W #imm,An` | 14 | 12 |
   | `ANDI.L #,Dn` | 14 | 16 |
   | `BSET/BCHG/BCLR` on Dn with bit < 16 | maximum | 2 fewer |
   | `MULS` | missed the last transition | `38 + 2n`, with n = 01/10 pairs in the 17-bit source |
   | `DIVU/DIVS` | fixed (140/158) | data-dependent, using the published division timing algorithm |

   The fallback interpreter and the tests apply the same adjustment (`cycles_adjust_previous`).
5. **Z80 reply seen mid-instruction:** the bus read happens near the end of the instruction. If
   a slice boundary falls before it, the Z80 has already run up to that boundary.
6. **Inputs:** they are recorded by time rather than by frame number (method point 3). This also
   removed 11.7 million interpreted steps: the game had been running different code.
7. **Scheduler bug:** a YM2610 timer made the next line be processed too early.
8. **Video write / line drawing order:**
   - a write to VRAM, palette or registers that ends at t_e is drawn on the line only if no
     earlier access ended at or after the start of that line;
   - in MOVE the write ends 4 cycles before the end of the instruction; in read-modify-write
     instructions it is the last access.
9. **YM sample rate:** MAME generates exactly 55,555 samples/s, not 55,555.55. The port's audio
   no longer drifts over time.

## Known residual differences

- **Sound, at the sample level.**
  - The port's Z80 sometimes services the YM2610 IRQ one instruction earlier than MAME's. The
    first case appears at 15.1 s.
  - Some sounds start 0 to 3 samples off (less than 0.1 ms). The signal energy matches second
    by second and there are stretches with an exact 1.0 correlation.
  - It does not affect the game: the 68000 receives the same replies on the same frames.
  - Matching it requires modeling when MAME's Z80 sees interrupts, as was done for the 68000.
- **Sampling classes not verified with traces:**
  - DIVU/DIVS with a memory operand;
  - CHK, TRAPV, RESET, MOVEP;
  - MOVEM is modeled as "last access at the end" based on the analysis.
- **Order of 32-bit writes:** with `MOVE.L` to `-(An)` the 68000 writes the low word first;
  the port always writes the high word first. It did not affect the recording.
