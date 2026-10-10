@echo off
rem Punto de entrada unico del instalador de Projecto Decompile.
rem Ejecuta setup.ps1 con ExecutionPolicy Bypass SOLO para este proceso.
rem Opciones (se pasan a setup.ps1):  -CopiarRoms  -SinRoms  -SoloVerificar  -Gog "ruta"
setlocal
set "PD_SETUP=%~dp0setup.ps1"
if not exist "%PD_SETUP%" set "PD_SETUP=%~dp0repo\entorno\setup.ps1"
if not exist "%PD_SETUP%" (
  echo No se encontro setup.ps1 ni en "%~dp0" ni en "%~dp0repo\entorno".
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PD_SETUP%" %*
set "PD_RC=%ERRORLEVEL%"
if not defined PD_SIN_PAUSA pause
exit /b %PD_RC%
