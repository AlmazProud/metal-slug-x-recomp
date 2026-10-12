# Projecto Decompile: portable environment

The whole environment lives in a single folder (`C:\Projecto Decompile`). It needs no
administrator rights, does not modify the global PATH and does not write to the registry.

```
Projecto Decompile\
  INSTALL.bat         single entry point of the installer
  env.ps1 / env.bat   activate the environment ONLY in the current session
  README.md           this file
  tools\              portable tools (+ _descargas, _cache, _verificacion)
  repo\               port source code (git). The installer lives in repo\setup\
  roms\               copy of YOUR ROMs (outside the repo, never versioned)
  analysis\           analysis output (outside the repo)
  logs\               installation logs
```

## Install or repair

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
- Then it asks whether you want to copy the ROMs from GOG to `roms\`. The default answer
  is **No**.

Options:

| Option | Effect |
|---|---|
| `-VerifyOnly` | Installs nothing; only runs the verification. |
| `-CopyRoms` | Only copies the ROMs (asks for confirmation). |
| `-NoRoms` | Does not ask about the ROMs at the end. |
| `-Gog "path"` | Path of the GOG installation (default `K:\Metal Slug X`). |

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
jdk21, ghidra\support (analyzeHeadless), mame, 7-Zip, imhex and git. It also sets `JAVA_HOME`,
`GHIDRA_INSTALL_DIR`, `CMAKE_PREFIX_PATH` (SDL3), `CMAKE_GENERATOR=Ninja`, `CC=clang`,
`PIP_CACHE_DIR` (inside tools\) and `PD_ROOT`, `PD_TOOLS`, `PD_REPO` and `PD_ROMS`.
The prompt shows `(PD)`.

## Update a tool

1. Edit `repo\setup\versions.json`: change the tool's `version`, `url` and `hash`.
   Take the hash from the official source given in `hash_source` (GitHub release digest,
   CMake SHA-256 file, Adoptium API, NuGet catalog).
2. Run `INSTALL.bat` again. Only the tool that changed is reinstalled.

Warning: updating Python reinstalls the whole `tools\python` folder and the packages installed
with pip are lost. They must be reinstalled from the repo's `requirements.txt`.

## Uninstall

Delete the `C:\Projecto Decompile` folder. Nothing is left in the registry or in the global PATH.
The Ghidra configuration, the pip cache and the ImHex configuration (portable mode) are
stored inside `tools\`.

## Repository rules

- `repo\` never contains ROMs, BIOS, graphics, audio or extracted data. The `.gitignore`
  works as a whitelist: it ignores everything and only lets code and documentation through.
- ROMs are read at runtime from `roms\` and validated by hash.
