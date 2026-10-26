@echo off
rem Activates the Metal Slug X port environment ONLY in this cmd window.
rem Does not touch the global PATH or the registry.
rem   call "C:\Projecto Decompile\env.bat"
rem Works both from the workspace root and from repo\setup (canonical copy).
rem Tools: <root>\tools if it exists, otherwise the shared ..\tools (C:\Projecto Decompile\tools).

set "PD_ROOT=%~dp0"
if not exist "%PD_ROOT%repo\" for %%I in ("%~dp0..\..") do set "PD_ROOT=%%~fI\"
set "PD_ROOT=%PD_ROOT:~0,-1%"
set "PD_TOOLS=%PD_ROOT%\tools"
if not exist "%PD_TOOLS%\" for %%I in ("%PD_ROOT%\..") do set "PD_TOOLS=%%~fI\tools"
set "PD_REPO=%PD_ROOT%\repo"
set "PD_ROMS=%PD_ROOT%\roms"

set "PD_7Z=%PD_TOOLS%\7zip"
if exist "%PD_TOOLS%\7zip\x64\7za.exe" set "PD_7Z=%PD_TOOLS%\7zip\x64"

if defined PD_ACTIVE goto :vars
set "PD_ACTIVE=1"
set "PATH=%PD_TOOLS%\python;%PD_TOOLS%\python\Scripts;%PD_TOOLS%\cmake\bin;%PD_TOOLS%\ninja;%PD_TOOLS%\llvm-mingw\bin;%PD_TOOLS%\sdl3\x86_64-w64-mingw32\bin;%PD_TOOLS%\jdk21\bin;%PD_TOOLS%\ghidra\support;%PD_TOOLS%\mame;%PD_7Z%;%PD_TOOLS%\imhex;%PD_TOOLS%\git\cmd;%PD_TOOLS%\gh\bin;%PATH%"
prompt (PD) $P$G

:vars
set "JAVA_HOME=%PD_TOOLS%\jdk21"
set "GHIDRA_INSTALL_DIR=%PD_TOOLS%\ghidra"
set "CMAKE_PREFIX_PATH=%PD_TOOLS%\sdl3\x86_64-w64-mingw32"
set "CMAKE_GENERATOR=Ninja"
set "CC=clang"
set "CXX=clang++"
set "PIP_CACHE_DIR=%PD_TOOLS%\_cache\pip"
set "PYTHONNOUSERSITE=1"
set "PYTHONUTF8=1"
set "GH_CONFIG_DIR=%PD_TOOLS%\_cache\gh"
echo Metal Slug X port environment active in this window (%PD_ROOT%, tools: %PD_TOOLS%)
