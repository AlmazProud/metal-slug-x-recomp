# ROM_MAP: GOG/DotEmu files and MAME's `mslugx` set

Source: GOG installation of Metal Slug X, folder `resources\game\`. The files are copied to
`roms\` with `INSTALL.bat -CopiarRoms`.

Oracle: MAME 0.289 (`mame -listxml mslugx`). Everything listed here was **verified by CRC32
and SHA1** with `tools/extract_roms.py`, which obtains the expected hashes from MAME at run
time.

## Summary

| DotEmu file | Bytes | Type | MAME ROM | Transformation | Verified |
|---|---|---|---|---|---|
| `mslugx_game_m68k` [0, 1 MB) | 1 048 576 | **P** (68000) | `250-p1.p1` | none | **DIFFERENT**: see P1 |
| `mslugx_game_m68k` [1 MB, 5 MB) | 4 194 304 | **P** (68000) | `250-p2.ep1` | none | OK |
| `mslugx_game_sfix` | 131 072 | **S** (game fix) | `250-s1.s1` | fix format (see below) | OK |
| `mslugx_game_z80` | 131 072 | **M** (Z80) | `250-m1.m1` | none | OK |
| `mslugx_adpcm` [0, 4 MB) | 4 194 304 | **V** (ADPCM-A) | `250-v1.v1` | none | OK |
| `mslugx_adpcm` [4 MB, 8 MB) | 4 194 304 | **V** | `250-v2.v2` | none | OK |
| `mslugx_adpcm` [8 MB, 10 MB) | 2 097 152 | **V** | `250-v3.v3` | none | OK |
| `mslugx_tiles` [0, 16 MB) | 16 777 216 | **C** (sprites) | `250-c1.c1` + `250-c2.c2` | sprite format (see below) | OK |
| `mslugx_tiles` [16 MB, 32 MB) | 16 777 216 | **C** | `250-c3.c3` + `250-c4.c4` | sprite format | OK |
| `mslugx_tiles` [32 MB, 48 MB) | 16 777 216 | **C** | `250-c5.c5` + `250-c6.c6` | sprite format | OK |
| `mslugx_bios_m68k` | 131 072 | 68000 BIOS | `sp-u2.sp1` (US MVS, `-bios us`) | none | OK |
| `mslugx_bios_sfix` | 131 072 | System fix | `sfix.sfix` | fix format | OK |
| `mslugx_zoom_table` | 131 072 | LSPC zoom table (L0) | `000-lo.lo` | none | OK |
| — | — | Z80 BIOS | `sm1.sm1` | **GOG does not include it**; a copy of M1 is used (see below) | STAND-IN |
| `mslugx_spr_usage` | 98 304 | DotEmu metadata | — | 2 bits per sprite tile (393 216 tiles) | n/a |
| `mslugx_gfix_usage` | 4 096 | DotEmu metadata | — | 1 byte per fix tile (4 096 tiles) | n/a |
| `mslugx_howtoplay`, `mslugx_stage`, `mslugx_multi_stage` | ~19 KB | DotEmu menu data | — | not part of the hardware | n/a |

## Formats

### P (68000)

DotEmu stores P1 and P2 concatenated, with the same byte order as MAME's `.p` files. MAME
loads them with `ROM_LOAD16_WORD_SWAP`, so the 68000 (big-endian) view is obtained by swapping
each byte pair. That view starts with the SSP vector `0x0010F300`, the PC vector `0x00C00402`
(BIOS entry point) and `NEO-GEO` at 0x100.

### S / sfix (fix layer, 8x8 tiles)

- **DotEmu:** 4 bits per pixel in row order. Each tile takes 32 bytes: 8 rows of 4 bytes, and
  each byte holds 2 pixels (the low nibble is the left one).
- **Neo Geo:** each 32-byte tile is organized by columns, in 4 groups of 8 bytes (one byte per
  row). Pixel pair k = 0, 1, 2, 3 (pixels 0-1, 2-3, 4-5, 6-7) goes in group 2, 3, 0, 1
  respectively. The byte is copied as is, without changing the nibble order.
- **How it was determined:** by trying the 24 group permutations × 2 nibble orders against
  MAME's CRC. Only one variant matched, the same one for `sfix.sfix` and for `250-s1.s1`.

### C (sprites, 16x16 tiles)

- **DotEmu:** 4 bits per pixel in row order. Each tile takes 128 bytes: 16 rows of 8 bytes,
  with the low nibble as the left pixel. Tiles are in the same order as in MAME's `sprites`
  region.
- **Neo Geo:** in the interleaved region (odd C in even bytes, even C in odd bytes), each tile
  takes 128 bytes:
  - the left half (x = 0–7) is at 0x40–0x7F and the right half (x = 8–15) at 0x00–0x3F;
  - each row uses 4 bytes: +0 plane 0, +1 plane 2, +2 plane 1, +3 plane 3;
  - within each plane byte, pixel x of the half occupies **bit 7 − x**.

  The odd C ROMs (c1, c3, c5) hold planes 0-1 and the even ones (c2, c4, c6), planes 2-3.
- **How it was determined:** with the per-tile layout documented in MAME (`neogeo_spr.cpp`,
  used only as a reference) and a sweep of 192 variants (nibble order, bit order, halves and
  plane assignment) against the CRC of `250-c1.c1`. There was a single match, and afterwards
  all 6 C ROMs matched in CRC32 and SHA1.
- The conversion is a bit permutation, therefore **reversible** and lossless.

## P1: contents differ from MAME's

- GOG's P1 gives CRC `60c33b1a` and SHA1 `8999bfc42b233cdb58778857c87d9f3187c61025`. MAME
  expects CRC `81f1f60b` and SHA1 `4c19f2e9824e606178ac1c9d4b0516fbaa625035`.
- Byte order does not explain it: P2 matches with the same treatment, and P1 was tried both
  as is and byte-swapped.
- The protection was **not** removed: the code keeps about 460 inline routines that read
  `0x2FFFE0` and the table at `0x0DEDD2`.
- Hypotheses still to verify: a DotEmu patch somewhere in P1, or a different P1 revision.
  Without the original P1 the difference cannot be located by direct comparison. It will be
  investigated in Phase 1 (disassembly). With GOG's P1, MAME boots and runs the attract mode
  normally.
- Consequence: the port and the oracle must use **the same P1, GOG's**. MAME accepts it with a
  checksum warning; it is not a fatal error.

## mslugx protection

Reference: MAME 0.289, files `src/devices/bus/neogeo/prot_mslugx.cpp`, `mslugx.cpp` and
`src/mame/snk/neogeo.cpp` (lines 1439-1440). They are used only as hardware documentation.

- **Hardware:** NEO-MVS PROGEOP board with an ALTERA EPM7128 CPLD connected to 250-P1.
- **I/O window:** `0x2FFFE0`–`0x2FFFEF` (16-bit reads and writes).
- **Internal state:** `command` (16 bits) and `counter`.

| Write to | Effect |
|---|---|
| `0x2FFFE0` | `command = 0` (start of read) |
| `0x2FFFE2`, `0x2FFFE4` | `command \|= data` (pulsed with the data and then with 0) |
| `0x2FFFE6` | no effect (end?) |
| `0x2FFFEA` | `counter = 0`, `command = 0` (initialization) |

**Read** (any address in the window): returns a single bit from a bit table stored in P1
itself, at `0x0DEDD2`, starting with the most significant bit:

- if `command = 0x0001`: returns bit `counter` of the table and then does `counter++`
  (MAME notes that this case is used in routines `$3BDC` and `$3C30`, register D7);
- if `command = 0x0FFF`: returns bit number `word[0x10F00A] − 1` (the rest of the accesses,
  register D2; MAME notes that the exact index calculation is uncertain);
- any other value: returns 0 and logs the access as unknown.

The game code repeats the pattern `lea $0DEDD2,a0 / lea $2FFFE0,a1 / ...` inline: about 460
references, for example at `0x0039BC`.

### Runtime confirmation

Checked with `tools/mame/trace_protection.lua`. The script intercepts (Lua taps) every read
and write in `0x2FFFE0`–`0x2FFFEF` and compares each read with our own model of the table
above, written from this documentation and not from MAME's device. Result over 120 s of boot
and *attract mode* (MAME 0.289, `-bios us`):

| Item | Value |
|---|---|
| Reads | 127 206, all equal to the model (0 differences) |
| Reads with `command=0x0001` | 12 420. From PC `0x003BDA` (8 192) and `0x003C2E` (4 228); matches MAME's note "$3bdc / $3c30". |
| Reads with `command=0x0FFF` | 114 786, almost all from PC `0x003A04` |
| Distinct PCs that read | 9: the inline routines rarely run in attract mode |
| Writes | `+0` 127 210, `+2` and `+4` 254 416 each, `+6` 229 592, `+A` 8 |

For the port, the protection can be reimplemented exactly like this in the memory bus
(Phase 3).

## Files MAME requires that GOG does not ship

- **`sm1.sm1`** (Z80 BIOS on the MVS board, CRC `94416d67`): without it, MAME does not boot
  (`Required files are missing`).
  - **Decision (option B, approved by the user):** the extractor writes a copy of
    `250-m1.m1` as `sm1.sm1`, marked as `SUSTITUTO` in the report. This way the Z80 boots
    straight from the game's sound program, just like on the Neo Geo AES (which has no SM1).
    MAME warns about the checksum but runs.
  - If the user places a genuine `sm1.sm1` in `roms/mame/neogeo/`, the extractor detects it by
    CRC and does not overwrite it.
  - **Verification:**
    - MAME boots, completes the BIOS and enters the game's attract mode.
    - In 60 s of recorded audio (`-wavwrite`) there are 44 s of sound at normal levels; the
      initial ~10 s of silence correspond to the BIOS boot.
  - **Residual risk:** the Z80 boot sequence differs from a real MVS until the BIOS switches
    to M1. It does not affect the port, whose runtime will boot the Z80 from M1 like the AES.
    If an audio divergence shows up at startup in Phase 4, this is the first suspect.
- To boot with the rebuilt set:
  `mame mslugx -bios us -rompath "<root>\roms\mame"`.
- The other BIOS options (`sp-s2`, uni-bios, etc.) are not needed when booting with
  `-bios us`.
