@echo off
rem Single entry point of the Metal Slug X port installer.
rem Runs setup.ps1 with ExecutionPolicy Bypass ONLY for this process.
rem Options (passed to setup.ps1):  -CopyRoms  -NoRoms  -VerifyOnly  -Gog "folder"  -Yes
setlocal
set "PD_SETUP=%~dp0setup.ps1"
if not exist "%PD_SETUP%" set "PD_SETUP=%~dp0repo\setup\setup.ps1"
if not exist "%PD_SETUP%" (
  echo setup.ps1 was not found in "%~dp0" nor in "%~dp0repo\setup".
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PD_SETUP%" %*
set "PD_RC=%ERRORLEVEL%"
if not defined PD_NO_PAUSE pause
exit /b %PD_RC%
