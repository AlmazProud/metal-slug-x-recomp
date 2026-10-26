# ADPCM-B: MAME's behaviour, learned as a black box

Metal Slug 3's game audio differed from MAME's `-wavwrite` from its first ADPCM-B sound on. FM,
SSG and ADPCM-A were identical, and so were the Z80's writes to the YM2610, so the difference was
inside the chip core. The port uses ymfm commit `81aec25`; MAME 0.289 bundles its own copy of
ymfm, which is not in the local reference. MAME's code was not read: its ADPCM-B was measured
from the outside, and the port's copy of ymfm was changed to match (`external/ymfm`, every change
marked `PORT CHANGE`).

Result: the scripted game run (`mslug3_start_audio_vs_mame`, 178 ADPCM-B sounds) and the attract
mode are identical to MAME sample for sample, as are the four probes below
(`mslug3_adpcmb_probes_vs_mame`).

## Method

- **MAME side (`tools/mame/ym_probe.lua`):**
  - MAME boots the game with its debugger;
  - the script stops the sound Z80 (`suspend`), silences the chip, and then writes YM2610
    registers itself at chosen frames;
  - MAME records the output (`-wavwrite`, 55 555 Hz), and the script logs the time of each
    frame's writes.
- **Port side (`tools/ymprobe/ym_probe.c`, target `ym_probe`):** the same writes go through the
  port's YM2610 (`src/neogeo/ym2610.cpp` and ymfm) at the same sample positions, with the same
  mix.
- **Probes** (scripts and recordings in `analysis\phase2\adpcmb`, outside the repo; all from a
  sample of the game's V ROM):

  | Probe | Content |
  |---|---|
  | 1 | Four playbacks: delta-N 0xBE74, delta-N 0x4000 stopped by the reset flag, delta-N changed one frame after the start, level 0x54 |
  | 2 | Eight one-page playbacks at two delta-N, different data at the end |
  | 3 | Four one-page playbacks at a slow delta-N (32 samples per nibble), to see the end's interpolation ramps |
  | 4 | Delta-N and level written after the start, as the game does |

- Models of the decoder were fitted to the recordings until they reproduced every sample. The
  study scripts are kept with the probes.

## MAME's ADPCM-B, as measured (differences from ymfm 81aec25)

- **Start:**
  - the output continues from its current value instead of jumping to 0; it interpolates from
    there towards the first decoded values;
  - the first position overflow after a start decodes nothing, so the decoder runs one nibble
    behind the position counter.
- **End of sample:**
  - two overflows before the last nibble would be reached, the **last nibble** (the low nibble
    of the end byte) is decoded over the decoder state of that moment;
  - playback ends and the EOS flag is set there;
  - the next clock freezes the output on that value: it is held, not set to 0.
  - ymfm instead decodes every nibble, sets EOS after the last one and clears the output.
- **Reset flag (register 0x10, bit 0):** stops playback and freezes the output on the last
  decoded value; ymfm clears the decoder.
- **Unchanged:** the decoder itself (step table, clamps), the linear interpolation, the level
  and pan, and when register writes take effect.

### Writes from Lua and from the Z80

MAME applies the writes the probe script makes from its frame notifier **one sample later** than
the same writes made by the Z80. With that delay (`ym_probe.c`), a single model reproduces both
the probes and the game's own writes. Without it, the probes fit a model whose output lags one
sample, and that model was one sample late in the game.

## Not covered

- **Repeat mode** (register 0x10, bit 4): not exercised by the probes or the game run. It keeps
  ymfm's behaviour.
- **The SSG:** in Metal Slug X's runs, a few SSG steps come one sample apart from MAME, with
  different intermediate values from the 2:9 resampler. That difference predates this work and
  is not studied yet. Metal Slug 3's runs so far are identical.
