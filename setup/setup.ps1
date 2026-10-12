#Requires -Version 5.1
<#
  Portable installer for Projecto Decompile (Environment phase).

  - Installs every tool into <root>\tools without administrator rights,
    without touching the global PATH or the registry.
  - Downloads only from official sources (versions.json) and verifies every hash.
    If a hash does not match, it stops.
  - Idempotent: skips what is installed, repairs what is missing or broken and
    reinstalls whatever changed version in versions.json.
  - At the end it verifies every tool, builds an SDL3 "hello world" and
    shows an OK/FAIL table.

  Parameters:
    -CopyRoms       Only copies the ROMs from GOG to roms\ (always asks for confirmation).
    -NoRoms         Does not ask about the ROMs at the end.
    -VerifyOnly     Installs nothing; only runs the verification.
    -Gog <path>     Path of the GOG installation (default: the one in versions.json).
#>
[CmdletBinding()]
param(
    [switch]$CopyRoms,
    [switch]$NoRoms,
    [switch]$VerifyOnly,
    [string]$Gog
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# ---------------------------------------------------------------- paths
$SetupDir  = $PSScriptRoot
$Repo      = Split-Path $SetupDir -Parent
$Root      = Split-Path $Repo -Parent
$Tools     = Join-Path $Root 'tools'
# On-disk names kept as-is so existing download caches stay valid.
$Downloads = Join-Path $Tools '_descargas'
$Verif     = Join-Path $Tools '_verificacion'
$Logs      = Join-Path $Root 'logs'
$Roms      = Join-Path $Root 'roms'
$Cfg       = Get-Content -LiteralPath (Join-Path $SetupDir 'versions.json') -Raw -Encoding UTF8 | ConvertFrom-Json

foreach ($d in @($Tools, $Downloads, $Logs)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d | Out-Null }
}
$LogFile = Join-Path $Logs ('install_{0}.log' -f (Get-Date -Format 'yyyyMMdd_HHmmss'))

function Log([string]$msg, [string]$color = 'Gray') {
    $line = '{0}  {1}' -f (Get-Date -Format 'HH:mm:ss'), $msg
    Add-Content -LiteralPath $LogFile -Value $line -Encoding UTF8
    Write-Host $msg -ForegroundColor $color
}
function Fail([string]$msg) {
    Log "ERROR: $msg" 'Red'
    throw $msg
}

# ---------------------------------------------------------------- processes
# Builds a command line following the CommandLineToArgvW rules, so that
# paths with spaces arrive intact.
function Format-Arg([string]$a) {
    if ($a -eq '') { return '""' }
    if ($a -notmatch '[\s"]') { return $a }
    $a = $a -replace '(\\*)"', '$1$1\"'
    $a = $a -replace '(\\+)$', '$1$1'
    return '"' + $a + '"'
}

# Runs a program and captures stdout+stderr. .bat/.cmd files are launched with
# cmd /d /s /c "..." so that cmd does not break the quotes when both the .bat
# and its arguments contain spaces.
function Invoke-Process {
    param(
        [Parameter(Mandatory)][string]$Exe,
        [string[]]$Arguments = @(),
        [string]$Dir = $null,
        [int]$TimeoutSec = 900
    )
    $argStr = ($Arguments | ForEach-Object { Format-Arg $_ }) -join ' '
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    if ($Exe -match '\.(bat|cmd)$') {
        $psi.FileName  = $env:ComSpec
        $psi.Arguments = '/d /s /c "' + (Format-Arg $Exe) + ' ' + $argStr + '"'
    } else {
        $psi.FileName  = $Exe
        $psi.Arguments = $argStr
    }
    $psi.UseShellExecute        = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError  = $true
    $psi.CreateNoWindow         = $true
    if ($Dir) { $psi.WorkingDirectory = $Dir }
    $p = [System.Diagnostics.Process]::Start($psi)
    $o = $p.StandardOutput.ReadToEndAsync()
    $e = $p.StandardError.ReadToEndAsync()
    if (-not $p.WaitForExit($TimeoutSec * 1000)) {
        try { $p.Kill() } catch { }
        return [pscustomobject]@{ Code = -1; Output = "Timed out ($TimeoutSec s)" }
    }
    $p.WaitForExit()
    [pscustomobject]@{ Code = $p.ExitCode; Output = ($o.Result + "`n" + $e.Result).Trim() }
}

# ---------------------------------------------------------------- hashes and downloads
function Get-FileHashLower([string]$path, [string]$algo) {
    (Get-FileHash -LiteralPath $path -Algorithm $algo).Hash.ToLowerInvariant()
}

function Get-Download($d) {
    $dest = Join-Path $Downloads $d.file
    $part = "$dest.part"
    $expected = $d.hash.ToLowerInvariant()
    if (Test-Path -LiteralPath $dest) {
        if ((Get-FileHashLower $dest $d.algorithm) -eq $expected) {
            Log "    cache OK: $($d.file)"
            return $dest
        }
        Log "    cached file has a different hash, downloading again: $($d.file)" 'Yellow'
        Remove-Item -LiteralPath $dest -Force
    }
    $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
    for ($attempt = 1; $attempt -le 2; $attempt++) {
        if ($attempt -eq 2 -and (Test-Path -LiteralPath $part)) { Remove-Item -LiteralPath $part -Force }
        Log "    downloading ($attempt/2): $($d.url)"
        $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        & $curl -L --fail --retry 3 --retry-delay 3 -C - --progress-bar -o $part $d.url
        $ErrorActionPreference = $old
        if (Test-Path -LiteralPath $part) {
            $actual = Get-FileHashLower $part $d.algorithm
            if ($actual -eq $expected) {
                Move-Item -LiteralPath $part -Destination $dest -Force
                Log "    $($d.algorithm) verified: $($d.file)" 'Green'
                return $dest
            }
            Log "    $($d.algorithm) mismatch (expected $expected, got $actual)" 'Yellow'
        }
    }
    if (Test-Path -LiteralPath $part) { Remove-Item -LiteralPath $part -Force }
    Fail ("HASH MISMATCH or download failed: $($d.file)`n  URL: $($d.url)`n" +
          "  Expected ($($d.algorithm)): $expected`n  Hash source: $($d.hash_source)`n" +
          "  The installer stops. Nothing of this tool was installed.")
}

# ---------------------------------------------------------------- 7-Zip
function Get-SevenZip {
    foreach ($c in @('7zip\x64\7za.exe', '7zip\7za.exe')) {
        $p = Join-Path $Tools $c
        if (Test-Path -LiteralPath $p) { return $p }
    }
    Fail '7za.exe is not available (the 7-Zip installation failed).'
}

function Expand-ArchiveFile([string]$sevenZip, [string]$archive, [string]$destination, [string]$type) {
    $a = @('x', '-y', '-bso0', '-bsp0', ('-o' + $destination))
    if ($type) { $a += ('-t' + $type) }
    $a += $archive
    $r = Invoke-Process -Exe $sevenZip -Arguments $a
    if ($r.Code -ne 0) { Fail "7-Zip could not extract $archive (code $($r.Code)):`n$($r.Output)" }
}

# ---------------------------------------------------------------- post-install tweaks
function Set-PostInstall($h, [string]$dest) {
    switch ($h.name) {
        'python' {
            $py = Join-Path $dest 'python.exe'
            $r = Invoke-Process -Exe $py -Arguments @('-m', 'pip', '--version')
            if ($r.Code -ne 0) {
                Log '    pip is not bundled; running ensurepip'
                $r = Invoke-Process -Exe $py -Arguments @('-m', 'ensurepip', '--default-pip')
                if ($r.Code -ne 0) { Fail "ensurepip failed:`n$($r.Output)" }
            }
        }
        'ghidra' {
            # Ghidra settings, cache and temp go to tools\_cache\ghidra instead of %APPDATA%,
            # %LOCALAPPDATA% or %TEMP%. A VMARGS with spaces breaks launch.bat (line 185, unquoted),
            # and LaunchSupport takes quotes literally. That is why the 8.3 short path is used; it is
            # recomputed on every run because it can change if the folder is recreated.
            $props = Join-Path $dest 'support\launch.properties'
            $lines = @(Get-Content -LiteralPath $props)
            $clean = @($lines | Where-Object {
                $_ -notmatch '^# --- Projecto Decompile' -and
                $_ -notmatch '^VMARGS=-D(application\.(settingsdir|cachedir|tempdir)|java\.io\.tmpdir)='
            })
            while ($clean.Count -gt 0 -and $clean[-1] -eq '') { $clean = @($clean[0..($clean.Count - 2)]) }

            $base = Join-Path $Tools '_cache\ghidra'
            $short = [ordered]@{}
            $fso = New-Object -ComObject Scripting.FileSystemObject
            foreach ($d in @('settings', 'cache', 'temp')) {
                $long = Join-Path $base $d
                if (-not (Test-Path -LiteralPath $long)) { New-Item -ItemType Directory -Path $long | Out-Null }
                $short[$d] = $fso.GetFolder($long).ShortPath.Replace('\', '/')
            }
            if (@($short.Values | Where-Object { $_ -match '\s' }).Count -gt 0) {
                Log '    ghidra: WARNING: this drive has no 8.3 short names; Ghidra will use %APPDATA%, %LOCALAPPDATA% and %TEMP%.' 'Yellow'
                $newLines = $clean
            } else {
                $newLines = $clean + @(
                    '',
                    '# --- Projecto Decompile: settings/cache/temp inside tools\_cache\ghidra (8.3 short path, no spaces) ---',
                    ('VMARGS=-Dapplication.settingsdir=' + $short['settings']),
                    ('VMARGS=-Dapplication.cachedir=' + $short['cache']),
                    ('VMARGS=-Dapplication.tempdir=' + $short['temp']),
                    ('VMARGS=-Djava.io.tmpdir=' + $short['temp'])
                )
            }
            if (($newLines -join "`n") -ne ($lines -join "`n")) {
                Set-Content -LiteralPath $props -Value $newLines -Encoding ASCII
                Log ('    ghidra: launch.properties points to ' + $short['settings'] + ' (and cache/temp)')
            }
        }
        'imhex' {
            # ImHex stores its configuration next to the executable when the 'portable' file exists.
            $flag = Join-Path $dest 'portable'
            if (-not (Test-Path -LiteralPath $flag)) { New-Item -ItemType File -Path $flag | Out-Null }
        }
    }
}

# ---------------------------------------------------------------- install one tool
function Install-Tool($h) {
    $dest   = Join-Path $Tools $h.name
    # Marker file name and its 'firma' key are kept as-is so existing installs are recognised.
    $marker = Join-Path $dest '.instalado.json'
    $signature = $h.version + '|' + ((@($h.downloads) | ForEach-Object { $_.hash }) -join ',')

    if ((Test-Path -LiteralPath $marker) -and (Test-Path -LiteralPath (Join-Path $dest $h.check))) {
        $m = Get-Content -LiteralPath $marker -Raw | ConvertFrom-Json
        if ($m.firma -eq $signature) {
            Log "[skip]       $($h.name) $($h.version) already installed" 'DarkGray'
            Set-PostInstall $h $dest   # idempotent: reapplies the current configuration
            return 'already installed'
        }
        Log "[update]     $($h.name) $($m.version) -> $($h.version)" 'Cyan'
        $action = 'updated'
    } elseif (Test-Path -LiteralPath $dest) {
        Log "[repair]     $($h.name) $($h.version) (incomplete or damaged installation)" 'Cyan'
        $action = 'repaired'
    } else {
        Log "[install]    $($h.name) $($h.version)" 'Cyan'
        $action = 'installed'
    }

    $files = @(foreach ($d in @($h.downloads)) { Get-Download $d })

    $tmp = Join-Path $Tools ('_tmp_' + $h.name)
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
    New-Item -ItemType Directory -Path $tmp | Out-Null

    if ($h.install -eq '7zip') {
        # Bootstrap: 7zr.exe (7z format only) extracts the 'extra' package, which ships 7za.exe.
        $sevenZr = Join-Path $tmp '7zr.exe'
        Copy-Item -LiteralPath $files[0] -Destination $sevenZr
        Expand-ArchiveFile $sevenZr $files[1] $tmp $null
        $src = $tmp
    } else {
        $type = $null
        if ($h.PSObject.Properties.Name -contains 'type') { $type = $h.type }
        Expand-ArchiveFile (Get-SevenZip) $files[0] $tmp $type
        $src = $tmp
        if ($h.PSObject.Properties.Name -contains 'subfolder') {
            $src = Join-Path $tmp $h.subfolder
        } else {
            $items = @(Get-ChildItem -LiteralPath $tmp -Force)
            if ($items.Count -eq 1 -and $items[0].PSIsContainer) { $src = $items[0].FullName }
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $src $h.check))) {
        Fail "After extracting $($h.name), '$($h.check)' is missing. Did the package layout change?"
    }

    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
    Move-Item -LiteralPath $src -Destination $dest
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }

    Set-PostInstall $h $dest

    [pscustomobject]@{
        name      = $h.name
        version   = $h.version
        firma     = $signature
        instalado = (Get-Date -Format 's')
    } | ConvertTo-Json | Set-Content -LiteralPath $marker -Encoding UTF8
    Log "    OK: $($h.name) in $dest" 'Green'
    return $action
}

# ---------------------------------------------------------------- root files
function Sync-Root {
    # env.*, README.md: the canonical copy lives in repo\setup and is mirrored to the root.
    foreach ($f in @('env.ps1', 'env.bat', 'README.md')) {
        $src = Join-Path $SetupDir $f
        $dst = Join-Path $Root $f
        if (-not (Test-Path -LiteralPath $src)) { continue }
        $same = (Test-Path -LiteralPath $dst) -and
                ((Get-FileHash -LiteralPath $src).Hash -eq (Get-FileHash -LiteralPath $dst).Hash)
        if (-not $same) { Copy-Item -LiteralPath $src -Destination $dst -Force; Log "    root: $f updated" }
    }
    # INSTALL.bat is only created if missing: overwriting a running .bat corrupts it.
    $bat = Join-Path $Root 'INSTALL.bat'
    if (-not (Test-Path -LiteralPath $bat)) { Copy-Item -LiteralPath (Join-Path $SetupDir 'INSTALL.bat') -Destination $bat }
}

# ---------------------------------------------------------------- ROMs
function Copy-Roms {
    $source = $Gog
    if (-not $source) { $source = $Cfg.paths.gog }
    $folder = Join-Path $source 'resources\game'
    if (-not (Test-Path -LiteralPath $folder)) {
        Log "'$folder' was not found. Give the path with -Gog ""path""." 'Yellow'
        return
    }
    $files = @(Get-ChildItem -LiteralPath $folder -File -Filter 'mslugx_*' | Sort-Object Name)
    if ($files.Count -eq 0) { Log "No mslugx_* files in '$folder'." 'Yellow'; return }

    Write-Host ''
    Write-Host "Files to copy from: $folder" -ForegroundColor Cyan
    $files | Format-Table @{ n = 'File'; e = { $_.Name } }, @{ n = 'Bytes'; e = { $_.Length }; a = 'Right' } -AutoSize | Out-String | Write-Host
    Write-Host "Destination: $Roms  (outside the repository; never versioned)"
    $resp = Read-Host 'Copy these files? [y/N]'
    if ($resp -notmatch '^[yY]') { Log 'ROMs: nothing copied (answer: no).' 'Yellow'; return }

    if (-not (Test-Path -LiteralPath $Roms)) { New-Item -ItemType Directory -Path $Roms | Out-Null }
    $manifest = New-Object System.Collections.Generic.List[string]
    foreach ($a in $files) {
        Copy-Item -LiteralPath $a.FullName -Destination (Join-Path $Roms $a.Name) -Force
        $manifest.Add(('{0} *{1}' -f (Get-FileHashLower (Join-Path $Roms $a.Name) 'SHA256'), $a.Name))
    }
    Set-Content -LiteralPath (Join-Path $Roms 'MANIFEST.sha256') -Value $manifest -Encoding ASCII
    Log "ROMs: $($files.Count) files copied to $Roms and MANIFEST.sha256 generated." 'Green'
}

# ---------------------------------------------------------------- repository
function Initialize-Repo {
    $git = Join-Path $Tools 'git\cmd\git.exe'
    if (-not (Test-Path -LiteralPath (Join-Path $Repo '.git'))) {
        $r = Invoke-Process -Exe $git -Arguments @('init', '-b', 'main') -Dir $Repo
        if ($r.Code -ne 0) { Fail "git init failed:`n$($r.Output)" }
        Log "    repo: git init in $Repo" 'Green'
    }
    # Versioned pre-commit hook (rule 1: never ROMs or binaries in the repo).
    $r = Invoke-Process -Exe $git -Arguments @('config', '--local', 'core.hooksPath', 'tools/hooks') -Dir $Repo
    if ($r.Code -ne 0) { Fail "could not set core.hooksPath:`n$($r.Output)" }
}

# ---------------------------------------------------------------- verification
function Test-Environment {
    . (Join-Path $SetupDir 'env.ps1') | Out-Null
    $res = New-Object System.Collections.Generic.List[object]
    function Add-Result($name, $ok, $detail) {
        $line = (($detail -split "`r?`n") | Where-Object { $_.Trim() -ne '' } | Select-Object -First 1)
        if (-not $line) { $line = '' }
        $res.Add([pscustomobject]@{ Test = $name; State = $(if ($ok) { 'OK' } else { 'FAIL' }); Detail = $line.Trim() })
        Log ("  [{0}] {1}: {2}" -f $(if ($ok) { 'OK' } else { 'FAIL' }), $name, $detail) $(if ($ok) { 'Green' } else { 'Red' })
    }
    function Test-Tool($name, $exe, $arguments, $pattern) {
        if (-not (Test-Path -LiteralPath $exe)) { Add-Result $name $false "$exe does not exist"; return }
        $r = Invoke-Process -Exe $exe -Arguments $arguments -TimeoutSec 120
        $output = $r.Output -replace "\x1b\[[0-9;]*m", ''   # strip ANSI colours (ImHex)
        $ok = ($output -match $pattern)
        $detail = $output
        if ($ok) {
            $line = (($output -split "`r?`n") | Where-Object { $_ -match $pattern } | Select-Object -First 1)
            $detail = $line.Substring([regex]::Match($line, $pattern).Index)
        }
        Add-Result $name $ok $detail
    }

    Log ''
    Log '== Verification ==' 'Cyan'
    $t = $Tools
    Test-Tool '7-Zip'    (Get-SevenZip) @('i') '7-Zip.*\d+\.\d+'
    Test-Tool 'Python'   "$t\python\python.exe" @('--version') 'Python 3\.\d+'
    Test-Tool 'pip'      "$t\python\python.exe" @('-m', 'pip', '--version') 'pip \d+'
    Test-Tool 'CMake'    "$t\cmake\bin\cmake.exe" @('--version') 'cmake version'
    Test-Tool 'Ninja'    "$t\ninja\ninja.exe" @('--version') '^\d+\.\d+'
    Test-Tool 'clang'    "$t\llvm-mingw\bin\clang.exe" @('--version') 'clang version'
    Test-Tool 'Java'     "$t\jdk21\bin\java.exe" @('-version') 'version "21'
    Test-Tool 'MAME'     "$t\mame\mame.exe" @('-version') '^\d+\.\d+'
    Test-Tool 'Git'      "$t\git\cmd\git.exe" @('--version') 'git version'

    # imhex.exe is the CLI (imhex-gui.exe is the GUI); --version opens no windows.
    Test-Tool 'ImHex'    "$t\imhex\imhex.exe" @('--version') 'Version:\s+\d+\.\d+'
    Test-Tool 'ImHex portable' "$t\imhex\imhex.exe" @('--version') 'Type:\s+Portable'

    # --- SDL3 "hello world": CMake + Ninja + clang + resources (.rc), all under the path with a space.
    $srcSdl  = Join-Path $SetupDir 'sdl3_test'
    $buildSdl = Join-Path $Verif 'build sdl3'
    if (Test-Path -LiteralPath $buildSdl) { Remove-Item -LiteralPath $buildSdl -Recurse -Force }
    $cmake = "$t\cmake\bin\cmake.exe"
    $r = Invoke-Process -Exe $cmake -Arguments @('-S', $srcSdl, '-B', $buildSdl, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_RC_COMPILER=llvm-windres')
    Add-Result 'SDL3: configure (CMake)' ($r.Code -eq 0) $(if ($r.Code -eq 0) { 'cmake configured in "' + $buildSdl + '"' } else { $r.Output })
    if ($r.Code -eq 0) {
        $r = Invoke-Process -Exe $cmake -Arguments @('--build', $buildSdl)
        Add-Result 'SDL3: build (Ninja+clang+windres)' ($r.Code -eq 0) $(if ($r.Code -eq 0) { 'hello_sdl3.exe built' } else { $r.Output })
        $exe = Join-Path $buildSdl 'hello_sdl3.exe'
        if ($r.Code -eq 0 -and (Test-Path -LiteralPath $exe)) {
            $r = Invoke-Process -Exe $exe -Arguments @('2000') -Dir $buildSdl -TimeoutSec 30
            Add-Result 'SDL3: window' ($r.Code -eq 0 -and $r.Output -match 'OK') $r.Output.Replace("`r", '').Replace("`n", ' | ')
        }
    }

    # --- Ghidra headless: creates a project and imports a PE inside the path with a space.
    $proj = Join-Path $Verif 'ghidra project'
    if (Test-Path -LiteralPath $proj) { Remove-Item -LiteralPath $proj -Recurse -Force }
    New-Item -ItemType Directory -Path $proj | Out-Null
    $sample = Join-Path $buildSdl 'hello_sdl3.exe'
    if (-not (Test-Path -LiteralPath $sample)) { $sample = "$t\ninja\ninja.exe" }
    $r = Invoke-Process -Exe "$t\ghidra\support\analyzeHeadless.bat" -Arguments @($proj, 'TestPD', '-import', $sample, '-noanalysis', '-deleteProject') -TimeoutSec 600
    $okG = ($r.Code -eq 0 -and $r.Output -match 'Import succeeded')
    $detG = $r.Output
    if ($okG) { $detG = 'analyzeHeadless imported ' + (Split-Path $sample -Leaf) + ' (Import succeeded)' }
    Add-Result 'Ghidra headless' $okG $detG

    # Ghidra must not leave anything outside the portable folder.
    $outside = @(@("$env:APPDATA\ghidra", "$env:LOCALAPPDATA\ghidra", "$env:TEMP\ghidra") | Where-Object { Test-Path -LiteralPath $_ })
    $detR = 'everything in tools\_cache\ghidra'
    if ($outside.Count -gt 0) { $detR = 'found: ' + ($outside -join ', ') + ' (if an older version created them, delete them and verify again)' }
    Add-Result 'Ghidra leaves nothing outside its folder' ($outside.Count -eq 0) $detR
    Add-Content -LiteralPath $LogFile -Value ("--- full Ghidra output ---`n" + $r.Output) -Encoding UTF8

    return $res
}

# ================================================================ main
try {
    Log "Projecto Decompile - installer ($((Get-Date).ToString('s')))" 'Cyan'
    Log "Root:  $Root"
    Log "Log:   $LogFile"

    if ($CopyRoms) { Copy-Roms; exit 0 }

    if (-not $VerifyOnly) {
        # 1. Do not run as administrator.
        $id = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        if ($id.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            Fail 'Running as administrator. Close this window and run INSTALL.bat as a normal user.'
        }
        # 2. Free space.
        $drive = (Get-Item -LiteralPath $Root).PSDrive
        $freeGB = [math]::Round($drive.Free / 1GB, 1)
        Log "Free space on $($drive.Name): $freeGB GB (minimum $($Cfg.min_free_space_gb) GB)"
        if ($freeGB -lt $Cfg.min_free_space_gb) { Fail "Not enough space: $freeGB GB free." }

        # 3. Tools (7-Zip first: it extracts everything else).
        $summary = New-Object System.Collections.Generic.List[object]
        foreach ($h in $Cfg.tools) {
            $acc = Install-Tool $h
            $summary.Add([pscustomobject]@{ Tool = $h.name; Version = $h.version; Action = $acc })
        }
        Log ''
        $summary | Format-Table -AutoSize | Out-String | ForEach-Object { Log $_ }

        # 4. Root and repository.
        Sync-Root
        Initialize-Repo
    }

    # 5. Verification.
    $res = Test-Environment
    Write-Host ''
    $table = $res | Format-Table Test, State, Detail -AutoSize -Wrap | Out-String -Width 160
    Write-Host $table
    Add-Content -LiteralPath $LogFile -Value $table -Encoding UTF8
    $failures = @($res | Where-Object { $_.State -ne 'OK' }).Count
    if ($failures -gt 0) { Log "$failures test(s) FAILED. See $LogFile" 'Red' }
    else { Log 'All tests OK.' 'Green' }

    # 6. ROMs (only with confirmation).
    if (-not $VerifyOnly -and -not $NoRoms) { Copy-Roms }

    Log ''
    Log 'To activate the environment in a session:' 'Cyan'
    Log "  PowerShell:  . `"$Root\env.ps1`""
    Log "  cmd:         call `"$Root\env.bat`""
    if ($failures -gt 0) { exit 2 } else { exit 0 }
}
catch {
    Log ("Installation stopped: " + $_.Exception.Message) 'Red'
    exit 1
}
