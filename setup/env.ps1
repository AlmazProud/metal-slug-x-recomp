# Activates the Projecto Decompile environment ONLY in the current PowerShell session.
# Does not touch the global PATH or the registry.
#
#   . "C:\Projecto Decompile\env.ps1"          (in an open session; note the leading dot)
#   powershell -NoExit -ExecutionPolicy Bypass -File "C:\Projecto Decompile\env.ps1"
#
# Works both from the root and from repo\setup (canonical copy).

$__pd = $PSScriptRoot
if (-not (Test-Path -LiteralPath (Join-Path $__pd 'tools'))) {
    $__pd = Split-Path (Split-Path $__pd -Parent) -Parent
}
$__t = Join-Path $__pd 'tools'

$env:PD_ROOT  = $__pd
$env:PD_TOOLS = $__t
$env:PD_REPO  = Join-Path $__pd 'repo'
$env:PD_ROMS  = Join-Path $__pd 'roms'

$__7z = Join-Path $__t '7zip\x64'
if (-not (Test-Path -LiteralPath (Join-Path $__7z '7za.exe'))) { $__7z = Join-Path $__t '7zip' }

$__dirs = @(
    (Join-Path $__t 'python'),
    (Join-Path $__t 'python\Scripts'),
    (Join-Path $__t 'cmake\bin'),
    (Join-Path $__t 'ninja'),
    (Join-Path $__t 'llvm-mingw\bin'),
    (Join-Path $__t 'sdl3\x86_64-w64-mingw32\bin'),
    (Join-Path $__t 'jdk21\bin'),
    (Join-Path $__t 'ghidra\support'),
    (Join-Path $__t 'mame'),
    $__7z,
    (Join-Path $__t 'imhex'),
    (Join-Path $__t 'git\cmd')
)
$__current = @($env:PATH -split ';' | Where-Object { $_ -ne '' })
$__new = @($__dirs | Where-Object { $__current -notcontains $_ })
if ($__new.Count -gt 0) { $env:PATH = (($__new + $__current) -join ';') }

$env:JAVA_HOME          = Join-Path $__t 'jdk21'
$env:GHIDRA_INSTALL_DIR = Join-Path $__t 'ghidra'
$env:CMAKE_PREFIX_PATH  = Join-Path $__t 'sdl3\x86_64-w64-mingw32'
$env:CMAKE_GENERATOR    = 'Ninja'
$env:CC                 = 'clang'
$env:CXX                = 'clang++'
$env:PIP_CACHE_DIR      = Join-Path $__t '_cache\pip'
$env:PYTHONNOUSERSITE   = '1'
$env:PYTHONUTF8         = '1'

if (-not (Test-Path Function:\__pd_prompt_original)) {
    Copy-Item Function:\prompt Function:\__pd_prompt_original
    function global:prompt { '(PD) ' + (& __pd_prompt_original) }
}

Remove-Variable __pd, __t, __7z, __dirs, __current, __new -ErrorAction SilentlyContinue
Write-Host "Projecto Decompile environment active in this session ($env:PD_ROOT)"
