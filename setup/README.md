# Metal Slug X port: portable environment

The workspace lives in `C:\Projecto Decompile`. It needs no administrator rights, does not modify
the global PATH and does not write to the registry. Its `tools\` folder is shared with the Metal
Slug, 3, 4 and 5 ports (`MS1_Port\`, `MS3_Port\`, `MS4_Port\`, `MS5_Port\` next to `repo\`);
nothing is downloaded twice.

```
Projecto Decompile\
  tools\              portable tools, shared (+ _descargas, _cache, _verificacion)
  INSTALL.bat         single entry point of the installer
  env.ps1 / env.bat   activate the environment ONLY in the current session
  README.md           this file
  grabar_partida.bat  records a game in MAME (coverage and verification)
  repo\               port source code (git). The installer lives in repo\setup\
  roms\               copy of YOUR ROMs (outside the repo, never versioned)
  analysis\           analysis output and MAME references (outside the repo)
  logs\               installation logs
```

## Install, repair or verify

Double-click `INSTALL.bat`, or from a console:

```
"C:\Projecto Decompile\INSTALL.bat"
```

- Downloads only from the official sources listed in `repo\setup\versions.json` and verifies
  the hash of every file. If any does not match, it stops and reports it.
- It is idempotent: it skips what is already installed, repairs what is missing or broken and
  reinstalls only what changed version.
- At the end it runs a verification (versions, an SDL3 "hello world" that opens a window for
  about 2 seconds, and an import with headless Ghidra) and shows an OK/FAIL table.
- Then it asks whether you want to copy the ROMs (the GOG copy's DotEmu `mslugx_*` files) to
  `roms\`. The default answer is **No**. The GOG install is only read.

Options:

| Option | Effect |
|---|---|
| `-VerifyOnly` | Installs nothing; only runs the verification. |
| `-CopyRoms` | Only copies the ROMs (asks for confirmation). |
| `-NoRoms` | Does not ask about the ROMs at the end. |
| `-Gog "folder"` | The GOG install of Metal Slug X (default `K:\Metal Slug X`). |
| `-Yes` | Copies the ROMs without asking. |

The files of `resources\game\mslugx_*` go to `roms\`. The MAME set `mslugx` rebuilt from them
(`python repo\tools\extract_roms.py`) goes to `roms\mame\mslugx\` and `roms\mame\neogeo\`, so MAME
finds it with `-rompath roms\mame`. `roms\MANIFEST.sha256` lists the copies.

The log of each run is written to `logs\install_YYYYMMDD_HHMMSS.log`.

## Activate the environment

PowerShell (in an open session; note the leading dot):

```
. "C:\Projecto Decompile\env.ps1"
```

Or open an already activated session:

```
powershell -NoExit -ExecutionPolicy Bypass -File "C:\Projecto Decompile\env.ps1"
```

cmd:

```
call "C:\Projecto Decompile\env.bat"
```

The environment prepends to the session PATH: python, cmake, ninja, llvm-mingw (clang), SDL3 (bin),
jdk21, ghidra\support (analyzeHeadless), mame, 7-Zip, imhex, git and gh. It also sets `JAVA_HOME`,
`GHIDRA_INSTALL_DIR`, `CMAKE_PREFIX_PATH` (SDL3), `CMAKE_GENERATOR=Ninja`, `CC=clang`,
`PIP_CACHE_DIR` (inside tools\), `GH_CONFIG_DIR` and `PD_ROOT` (the workspace), `PD_TOOLS`,
`PD_REPO` and `PD_ROMS`. The prompt shows `(PD)`.

## Update a tool

1. Edit `repo\setup\versions.json`: change the tool's `version`, `url` and `hash`.
   Take the hash from the official source given in `hash_source` (GitHub release digest,
   CMake SHA-256 file, Adoptium API, NuGet catalog).
2. Run `INSTALL.bat` again. Only the tool that changed is reinstalled.

Warning: the tools folder is shared with the Metal Slug, 3, 4 and 5 ports, so an update affects
all of them. Updating Python reinstalls the whole `tools\python` folder and the packages installed
with pip are lost. They must be reinstalled from the repo's `tools\requirements.txt`.

## Repository rules

- `repo\` never contains ROMs, BIOS, graphics, audio or extracted data. The `.gitignore`
  works as a whitelist: it ignores everything and only lets code and documentation through.
- ROMs are read at runtime from `roms\` and validated by hash.
