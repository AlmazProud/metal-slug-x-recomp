# Musashi in Projecto Decompile

- **Source:** https://github.com/kstenerud/Musashi, commit `313ebf1bd9f4d0d93341eb5ce21fd8a119e9dbdd`
  (2026-03-08), version 4.60.
- **Package:** codeload.github.com, `Musashi-313ebf1.tar.gz`, SHA256
  `8bed9e28e7e5377256d41aff80a99c9e917a24e7392d9219e1c45c40ad4000e5`.
- **License:** MIT (Copyright Karl Stenerud). The text is in `readme.txt` and in the header of
  every file.

## Use

1. Reference interpreter that validates the semantics of every instruction in the recompiler's
   differential tests (Phase 2).
2. Fallback interpreter of the runtime: code in RAM, unresolved jumps and code not seen by the
   analysis.

## Original files, unmodified

`m68k.h`, `m68k_in.c`, `m68kconf.h`, `m68kcpu.c`, `m68kcpu.h`, `m68kdasm.c`, `m68kmake.c`,
`m68kmmu.h`, `readme.txt`, `history.txt`.

Configuration is done with compile definitions (`CMakeLists.txt`), not by editing `m68kconf.h`:
plain 68000, no 68010/020/030/040, no PMMU.

## Replaced files (our own)

The 68000 has no FPU. To avoid shipping **SoftFloat 2b**, whose license carries an
indemnification clause (the user's decision, option B), these files are replaced:

| File | Original | Replacement |
|---|---|---|
| `m68kfpu.c` | 68881/68040 FPU emulation (uses SoftFloat) | FPU instructions raise the Line-F exception, as on a real 68000. PMMU accesses abort; with the PMMU disabled they never run. |
| `softfloat/milieu.h` | SoftFloat types | Only the integer type names Musashi expects |
| `softfloat/softfloat.h` | SoftFloat API | Only the `floatx80` type of the FPU registers in the CPU struct |

No file from the original package's `softfloat/` is included.

## Verification

Booting the user's BIOS from reset, Musashi's PC sequence matches the MAME 0.289 debugger trace
(`C00402 → C11002 → C11006 → … → C185D6`).
