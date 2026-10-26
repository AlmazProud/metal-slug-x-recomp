@echo off
rem Records a Metal Slug X game (MAME set mslugx, rebuilt by tools\extract_roms.py in roms\mame) in
rem MAME 0.289 (coverage and verification). It runs from the workspace root (where it is
rem grabar_partida.bat); this is the canonical copy.
rem Usage: grabar_partida.bat [name]   (default name: partida1)
rem The recording starts with an empty NVRAM, like the scripted runs, so it can be replayed exactly.
setlocal
set NAME=%~1
if "%NAME%"=="" set NAME=partida1
set ROOT=%~dp0
set OUT=%ROOT%analysis\phase3\play
set WORK=%OUT%\_mame_%NAME%
if exist "%OUT%\%NAME%.inp" (
  echo %OUT%\%NAME%.inp already exists: choose another name.
  exit /b 1
)
if exist "%WORK%\nvram" rmdir /s /q "%WORK%\nvram"
mkdir "%WORK%\nvram" 2>nul
mkdir "%OUT%\cfg" 2>nul
set MAMEDIR=%ROOT%tools\mame
if not exist "%MAMEDIR%\mame.exe" set MAMEDIR=%ROOT%..\tools\mame
pushd "%MAMEDIR%"
mame.exe mslugx -bios us -rompath "%ROOT%roms\mame" -nvram_directory "%WORK%\nvram" ^
  -cfg_directory "%OUT%\cfg" -input_directory "%OUT%" -record "%NAME%.inp" -skip_gameinfo
popd
echo Recording: %OUT%\%NAME%.inp
endlocal
