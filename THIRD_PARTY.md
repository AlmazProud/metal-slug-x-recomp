# Third parties

## Libraries integrated into the port's code

Each library is listed with its version, origin, license and how it is used. They are copied
unmodified, together with their license file.

| Library | Version | Origin | License | Use |
|---|---|---|---|---|
| Musashi | 4.60 (commit `313ebf1`) | github.com/kstenerud/Musashi, copied into `external/musashi/` | MIT | Reference 68000 interpreter (differential tests) and runtime fallback. **No SoftFloat**: `m68kfpu.c` and the `softfloat/` headers are our own substitutes (see `external/musashi/LEEME_PD.md`). Linked into the port. |
| z80.h (chips) | commit `9e88298` (2026-09-26); tarball SHA256 `fcae3999…a09b8d` | github.com/floooh/chips, `chips/z80.h` copied into `external/chips/` with its `LICENSE` | zlib | Sound Z80, emulated cycle by cycle. Linked into the port. |
| ymfm | commit `81aec25` (2026-07-27); tarball SHA256 `9bcf001b…f443f` | github.com/aaronsgiles/ymfm, only the YM2610 files (OPN, ADPCM and SSG) in `external/ymfm/` with its `LICENSE` | BSD-3-Clause | YM2610 (FM, SSG, ADPCM-A/B), the same core MAME uses. Written in C++: it is the only C++ code in the port, used through a C bridge (`src/neogeo/ym2610.cpp`). Linked into the port, with the C++ runtime linked statically. |
| Capstone | 5.0.7 | PyPI (`capstone`), github.com/capstone-engine/capstone | BSD-3-Clause | 68000 instruction decoder in the analysis tools (Phase 1). Not linked into the port. |
| NumPy | 2.5.4 | PyPI (`numpy`) | BSD-3-Clause | Graphics format conversion in the ROM extractor. Not linked into the port. |

## Environment tools (not distributed with the repo)

The installer (`setup/setup.ps1`) downloads them into `tools\` from their official sources.
None of them is linked into or redistributed with the port.

| Tool | License | Use |
|---|---|---|
| SDL3 | zlib | Runtime window, input and audio. Linked dynamically (SDL3.dll). |
| llvm-mingw | Apache-2.0 with LLVM exception, plus the mingw-w64 components | C compiler |
| CMake | BSD-3-Clause | Build |
| Ninja | Apache-2.0 | Build |
| Python (CPython) | PSF-2.0 | Analysis tools |
| Eclipse Temurin JDK 21 | GPL-2.0 with Classpath Exception | Required by Ghidra |
| Ghidra | Apache-2.0 | Disassembly and analysis |
| MAME | GPL-2.0+ (the project as a whole) | Oracle and debugger. Its source code is used only as hardware documentation reference and is never copied. |
| 7-Zip | LGPL-2.1 with unRAR restriction | Decompression |
| ImHex | GPL-2.0 | Hex editor |
| MinGit (Git for Windows) | GPL-2.0 | Version control |
