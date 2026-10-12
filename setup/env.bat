@echo off
rem Activa el entorno de Projecto Decompile SOLO en esta window de cmd.
rem No toca el PATH global ni el registro.
rem   call "C:\Projecto Decompile\env.bat"
rem Funciona tanto desde la raiz como desde repo\setup (copia canonica).

set "PD_ROOT=%~dp0"
if not exist "%PD_ROOT%tools\" for %%I in ("%~dp0..\..") do set "PD_ROOT=%%~fI\"
set "PD_ROOT=%PD_ROOT:~0,-1%"
set "PD_TOOLS=%PD_ROOT%\tools"
set "PD_REPO=%PD_ROOT%\repo"
set "PD_ROMS=%PD_ROOT%\roms"

set "PD_7Z=%PD_TOOLS%\7zip"
if exist "%PD_TOOLS%\7zip\x64\7za.exe" set "PD_7Z=%PD_TOOLS%\7zip\x64"

if defined PD_ACTIVO goto :vars
set "PD_ACTIVO=1"
set "PATH=%PD_TOOLS%\python;%PD_TOOLS%\python\Scripts;%PD_TOOLS%\cmake\bin;%PD_TOOLS%\ninja;%PD_TOOLS%\llvm-mingw\bin;%PD_TOOLS%\sdl3\x86_64-w64-mingw32\bin;%PD_TOOLS%\jdk21\bin;%PD_TOOLS%\ghidra\support;%PD_TOOLS%\mame;%PD_7Z%;%PD_TOOLS%\imhex;%PD_TOOLS%\git\cmd;%PATH%"
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
echo Entorno Projecto Decompile active en esta window (%PD_ROOT%)
