#Requires -Version 5.1
<#
  Instalador portable de Projecto Decompile (Fase Entorno).

  - Instala todas las herramientas en <raíz>\tools sin permisos de administrador,
    sin tocar el PATH global ni el registro.
  - Descarga solo de fuentes oficiales (versions.json) y verifica cada hash.
    Si un hash no coincide, se detiene.
  - Idempotente: salta lo instalado, repara lo que falte o esté roto y
    reinstala lo que cambió de versión en versions.json.
  - Al final verifica cada herramienta, compila un "hola mundo" con SDL3 y
    muestra una tabla OK/FALLA.

  Parámetros:
    -CopiarRoms     Solo copia los ROMs desde GOG a roms\ (siempre pide confirmación).
    -SinRoms        No pregunta por los ROMs al final.
    -SoloVerificar  No instala nada; solo ejecuta la verificación.
    -Gog <ruta>     Ruta de la instalación GOG (por defecto, la de versions.json).
#>
[CmdletBinding()]
param(
    [switch]$CopiarRoms,
    [switch]$SinRoms,
    [switch]$SoloVerificar,
    [string]$Gog
)

Set-StrictMode -Version 2
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# ---------------------------------------------------------------- rutas
$Entorno   = $PSScriptRoot
$Repo      = Split-Path $Entorno -Parent
$Root      = Split-Path $Repo -Parent
$Tools     = Join-Path $Root 'tools'
$Descargas = Join-Path $Tools '_descargas'
$Verif     = Join-Path $Tools '_verificacion'
$Logs      = Join-Path $Root 'logs'
$Roms      = Join-Path $Root 'roms'
$Cfg       = Get-Content -LiteralPath (Join-Path $Entorno 'versions.json') -Raw -Encoding UTF8 | ConvertFrom-Json

foreach ($d in @($Tools, $Descargas, $Logs)) {
    if (-not (Test-Path -LiteralPath $d)) { New-Item -ItemType Directory -Path $d | Out-Null }
}
$LogFile = Join-Path $Logs ('instalacion_{0}.log' -f (Get-Date -Format 'yyyyMMdd_HHmmss'))

function Log([string]$msg, [string]$color = 'Gray') {
    $linea = '{0}  {1}' -f (Get-Date -Format 'HH:mm:ss'), $msg
    Add-Content -LiteralPath $LogFile -Value $linea -Encoding UTF8
    Write-Host $msg -ForegroundColor $color
}
function Fallo([string]$msg) {
    Log "ERROR: $msg" 'Red'
    throw $msg
}

# ---------------------------------------------------------------- procesos
# Construye una línea de comandos con las reglas de CommandLineToArgvW, para que
# las rutas con espacios lleguen intactas.
function Format-Arg([string]$a) {
    if ($a -eq '') { return '""' }
    if ($a -notmatch '[\s"]') { return $a }
    $a = $a -replace '(\\*)"', '$1$1\"'
    $a = $a -replace '(\\+)$', '$1$1'
    return '"' + $a + '"'
}

# Ejecuta un programa y captura stdout+stderr. Los .bat/.cmd se lanzan con
# cmd /d /s /c "..." para que cmd no rompa las comillas cuando tanto el .bat
# como sus argumentos llevan espacios.
function Invoke-Proceso {
    param(
        [Parameter(Mandatory)][string]$Exe,
        [string[]]$Argumentos = @(),
        [string]$Dir = $null,
        [int]$TimeoutSeg = 900
    )
    $argStr = ($Argumentos | ForEach-Object { Format-Arg $_ }) -join ' '
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
    if (-not $p.WaitForExit($TimeoutSeg * 1000)) {
        try { $p.Kill() } catch { }
        return [pscustomobject]@{ Codigo = -1; Salida = "Tiempo agotado ($TimeoutSeg s)" }
    }
    $p.WaitForExit()
    [pscustomobject]@{ Codigo = $p.ExitCode; Salida = ($o.Result + "`n" + $e.Result).Trim() }
}

# ---------------------------------------------------------------- hashes y descargas
function Get-HashArchivo([string]$ruta, [string]$algo) {
    (Get-FileHash -LiteralPath $ruta -Algorithm $algo).Hash.ToLowerInvariant()
}

function Get-Descarga($d) {
    $dest = Join-Path $Descargas $d.archivo
    $part = "$dest.part"
    $esperado = $d.hash.ToLowerInvariant()
    if (Test-Path -LiteralPath $dest) {
        if ((Get-HashArchivo $dest $d.algoritmo) -eq $esperado) {
            Log "    caché OK: $($d.archivo)"
            return $dest
        }
        Log "    caché con hash distinto, se vuelve a descargar: $($d.archivo)" 'Yellow'
        Remove-Item -LiteralPath $dest -Force
    }
    $curl = Join-Path $env:SystemRoot 'System32\curl.exe'
    for ($intento = 1; $intento -le 2; $intento++) {
        if ($intento -eq 2 -and (Test-Path -LiteralPath $part)) { Remove-Item -LiteralPath $part -Force }
        Log "    descargando ($intento/2): $($d.url)"
        $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        & $curl -L --fail --retry 3 --retry-delay 3 -C - --progress-bar -o $part $d.url
        $ErrorActionPreference = $old
        if (Test-Path -LiteralPath $part) {
            $obtenido = Get-HashArchivo $part $d.algoritmo
            if ($obtenido -eq $esperado) {
                Move-Item -LiteralPath $part -Destination $dest -Force
                Log "    $($d.algoritmo) verificado: $($d.archivo)" 'Green'
                return $dest
            }
            Log "    $($d.algoritmo) no coincide (esperado $esperado, obtenido $obtenido)" 'Yellow'
        }
    }
    if (Test-Path -LiteralPath $part) { Remove-Item -LiteralPath $part -Force }
    Fallo ("HASH NO COINCIDE o descarga fallida: $($d.archivo)`n  URL: $($d.url)`n" +
           "  Esperado ($($d.algoritmo)): $esperado`n  Fuente del hash: $($d.fuente_hash)`n" +
           "  El instalador se detiene. No se instaló nada de esta herramienta.")
}

# ---------------------------------------------------------------- 7-Zip
function Get-SieteZip {
    foreach ($c in @('7zip\x64\7za.exe', '7zip\7za.exe')) {
        $p = Join-Path $Tools $c
        if (Test-Path -LiteralPath $p) { return $p }
    }
    Fallo '7za.exe no está disponible (la instalación de 7-Zip falló).'
}

function Expand-Archivo([string]$sieteZip, [string]$archivo, [string]$destino, [string]$tipo) {
    $a = @('x', '-y', '-bso0', '-bsp0', ('-o' + $destino))
    if ($tipo) { $a += ('-t' + $tipo) }
    $a += $archivo
    $r = Invoke-Proceso -Exe $sieteZip -Argumentos $a
    if ($r.Codigo -ne 0) { Fallo "7-Zip no pudo extraer $archivo (código $($r.Codigo)):`n$($r.Salida)" }
}

# ---------------------------------------------------------------- ajustes posteriores
function Set-PostInstalacion($h, [string]$dest) {
    switch ($h.nombre) {
        'python' {
            $py = Join-Path $dest 'python.exe'
            $r = Invoke-Proceso -Exe $py -Argumentos @('-m', 'pip', '--version')
            if ($r.Codigo -ne 0) {
                Log '    pip no viene incluido; ejecutando ensurepip'
                $r = Invoke-Proceso -Exe $py -Argumentos @('-m', 'ensurepip', '--default-pip')
                if ($r.Codigo -ne 0) { Fallo "ensurepip falló:`n$($r.Salida)" }
            }
        }
        'ghidra' {
            # PENDIENTE (decisión del usuario): redirigir settings/cache/temp de Ghidra a tools\.
            # Un VMARGS con espacios rompe launch.bat (línea 185, sin comillas) y LaunchSupport
            # toma las comillas literalmente. Mientras tanto se deja launch.properties original
            # y se limpia cualquier línea que haya agregado una versión anterior del instalador.
            $props = Join-Path $dest 'support\launch.properties'
            $lineas = @(Get-Content -LiteralPath $props)
            $limpias = @($lineas | Where-Object { $_ -notmatch 'Projecto Decompile' })
            if ($limpias.Count -ne $lineas.Count) {
                Set-Content -LiteralPath $props -Value $limpias -Encoding ASCII
                Log '    ghidra: se retiraron líneas VMARGS de una versión anterior del instalador' 'Yellow'
            }
        }
        'imhex' {
            # ImHex guarda su configuración junto al ejecutable cuando existe el archivo 'portable'.
            $marca = Join-Path $dest 'portable'
            if (-not (Test-Path -LiteralPath $marca)) { New-Item -ItemType File -Path $marca | Out-Null }
        }
    }
}

# ---------------------------------------------------------------- instalar una herramienta
function Install-Herramienta($h) {
    $dest  = Join-Path $Tools $h.nombre
    $marca = Join-Path $dest '.instalado.json'
    $firma = $h.version + '|' + ((@($h.descargas) | ForEach-Object { $_.hash }) -join ',')

    if ((Test-Path -LiteralPath $marca) -and (Test-Path -LiteralPath (Join-Path $dest $h.comprobar))) {
        $m = Get-Content -LiteralPath $marca -Raw | ConvertFrom-Json
        if ($m.firma -eq $firma) {
            Log "[saltar]     $($h.nombre) $($h.version) ya instalado" 'DarkGray'
            Set-PostInstalacion $h $dest   # idempotente: reaplica la configuración vigente
            return 'ya instalado'
        }
        Log "[actualizar] $($h.nombre) $($m.version) -> $($h.version)" 'Cyan'
        $accion = 'actualizado'
    } elseif (Test-Path -LiteralPath $dest) {
        Log "[reparar]    $($h.nombre) $($h.version) (instalación incompleta o dañada)" 'Cyan'
        $accion = 'reparado'
    } else {
        Log "[instalar]   $($h.nombre) $($h.version)" 'Cyan'
        $accion = 'instalado'
    }

    $archivos = @(foreach ($d in @($h.descargas)) { Get-Descarga $d })

    $tmp = Join-Path $Tools ('_tmp_' + $h.nombre)
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }
    New-Item -ItemType Directory -Path $tmp | Out-Null

    if ($h.instalar -eq '7zip') {
        # Arranque: 7zr.exe (solo formato 7z) extrae el paquete 'extra', que trae 7za.exe.
        $sieteZr = Join-Path $tmp '7zr.exe'
        Copy-Item -LiteralPath $archivos[0] -Destination $sieteZr
        Expand-Archivo $sieteZr $archivos[1] $tmp $null
        $src = $tmp
    } else {
        $tipo = $null
        if ($h.PSObject.Properties.Name -contains 'tipo') { $tipo = $h.tipo }
        Expand-Archivo (Get-SieteZip) $archivos[0] $tmp $tipo
        $src = $tmp
        if ($h.PSObject.Properties.Name -contains 'subcarpeta') {
            $src = Join-Path $tmp $h.subcarpeta
        } else {
            $items = @(Get-ChildItem -LiteralPath $tmp -Force)
            if ($items.Count -eq 1 -and $items[0].PSIsContainer) { $src = $items[0].FullName }
        }
    }
    if (-not (Test-Path -LiteralPath (Join-Path $src $h.comprobar))) {
        Fallo "Tras extraer $($h.nombre) no aparece '$($h.comprobar)'. ¿Cambió la estructura del paquete?"
    }

    if (Test-Path -LiteralPath $dest) { Remove-Item -LiteralPath $dest -Recurse -Force }
    Move-Item -LiteralPath $src -Destination $dest
    if (Test-Path -LiteralPath $tmp) { Remove-Item -LiteralPath $tmp -Recurse -Force }

    Set-PostInstalacion $h $dest

    [pscustomobject]@{
        nombre    = $h.nombre
        version   = $h.version
        firma     = $firma
        instalado = (Get-Date -Format 's')
    } | ConvertTo-Json | Set-Content -LiteralPath $marca -Encoding UTF8
    Log "    OK: $($h.nombre) en $dest" 'Green'
    return $accion
}

# ---------------------------------------------------------------- archivos de la raíz
function Sync-Raiz {
    # env.*, LEEME.md: la copia canónica vive en repo\entorno y se refleja en la raíz.
    foreach ($f in @('env.ps1', 'env.bat', 'LEEME.md')) {
        $src = Join-Path $Entorno $f
        $dst = Join-Path $Root $f
        if (-not (Test-Path -LiteralPath $src)) { continue }
        $igual = (Test-Path -LiteralPath $dst) -and
                 ((Get-FileHash -LiteralPath $src).Hash -eq (Get-FileHash -LiteralPath $dst).Hash)
        if (-not $igual) { Copy-Item -LiteralPath $src -Destination $dst -Force; Log "    raíz: $f actualizado" }
    }
    # INSTALAR.bat solo se crea si falta: sobrescribir un .bat en ejecución lo corrompe.
    $bat = Join-Path $Root 'INSTALAR.bat'
    if (-not (Test-Path -LiteralPath $bat)) { Copy-Item -LiteralPath (Join-Path $Entorno 'INSTALAR.bat') -Destination $bat }
}

# ---------------------------------------------------------------- ROMs
function Copy-Roms {
    $origen = $Gog
    if (-not $origen) { $origen = $Cfg.rutas.gog }
    $carpeta = Join-Path $origen 'resources\game'
    if (-not (Test-Path -LiteralPath $carpeta)) {
        Log "No se encontró '$carpeta'. Indica la ruta con -Gog ""ruta""." 'Yellow'
        return
    }
    $archivos = @(Get-ChildItem -LiteralPath $carpeta -File -Filter 'mslugx_*' | Sort-Object Name)
    if ($archivos.Count -eq 0) { Log "No hay archivos mslugx_* en '$carpeta'." 'Yellow'; return }

    Write-Host ''
    Write-Host "Archivos a copiar desde: $carpeta" -ForegroundColor Cyan
    $archivos | Format-Table @{ n = 'Archivo'; e = { $_.Name } }, @{ n = 'Bytes'; e = { $_.Length }; a = 'Right' } -AutoSize | Out-String | Write-Host
    Write-Host "Destino: $Roms  (fuera del repositorio; nunca se versiona)"
    $resp = Read-Host '¿Copiar estos archivos? [s/N]'
    if ($resp -notmatch '^[sS]') { Log 'ROMs: no se copió nada (respuesta: no).' 'Yellow'; return }

    if (-not (Test-Path -LiteralPath $Roms)) { New-Item -ItemType Directory -Path $Roms | Out-Null }
    $manifiesto = New-Object System.Collections.Generic.List[string]
    foreach ($a in $archivos) {
        Copy-Item -LiteralPath $a.FullName -Destination (Join-Path $Roms $a.Name) -Force
        $manifiesto.Add(('{0} *{1}' -f (Get-HashArchivo (Join-Path $Roms $a.Name) 'SHA256'), $a.Name))
    }
    Set-Content -LiteralPath (Join-Path $Roms 'MANIFEST.sha256') -Value $manifiesto -Encoding ASCII
    Log "ROMs: $($archivos.Count) archivos copiados a $Roms y MANIFEST.sha256 generado." 'Green'
}

# ---------------------------------------------------------------- repositorio
function Initialize-Repo {
    $git = Join-Path $Tools 'git\cmd\git.exe'
    if (Test-Path -LiteralPath (Join-Path $Repo '.git')) { return }
    $r = Invoke-Proceso -Exe $git -Argumentos @('init', '-b', 'main') -Dir $Repo
    if ($r.Codigo -ne 0) { Fallo "git init falló:`n$($r.Salida)" }
    Log "    repo: git init en $Repo" 'Green'
}

# ---------------------------------------------------------------- verificación
function Test-Entorno {
    . (Join-Path $Entorno 'env.ps1') | Out-Null
    $res = New-Object System.Collections.Generic.List[object]
    function Agregar($nombre, $ok, $detalle) {
        $linea = (($detalle -split "`r?`n") | Where-Object { $_.Trim() -ne '' } | Select-Object -First 1)
        if (-not $linea) { $linea = '' }
        $res.Add([pscustomobject]@{ Prueba = $nombre; Estado = $(if ($ok) { 'OK' } else { 'FALLA' }); Detalle = $linea.Trim() })
        Log ("  [{0}] {1}: {2}" -f $(if ($ok) { 'OK' } else { 'FALLA' }), $nombre, $detalle) $(if ($ok) { 'Green' } else { 'Red' })
    }
    function Probar($nombre, $exe, $argumentos, $patron) {
        if (-not (Test-Path -LiteralPath $exe)) { Agregar $nombre $false "no existe $exe"; return }
        $r = Invoke-Proceso -Exe $exe -Argumentos $argumentos -TimeoutSeg 120
        $salida = $r.Salida -replace "\x1b\[[0-9;]*m", ''   # quita colores ANSI (ImHex)
        $ok = ($salida -match $patron)
        $detalle = $salida
        if ($ok) {
            $linea = (($salida -split "`r?`n") | Where-Object { $_ -match $patron } | Select-Object -First 1)
            $detalle = $linea.Substring([regex]::Match($linea, $patron).Index)
        }
        Agregar $nombre $ok $detalle
    }

    Log ''
    Log '== Verificación ==' 'Cyan'
    $t = $Tools
    Probar '7-Zip'    (Get-SieteZip) @('i') '7-Zip.*\d+\.\d+'
    Probar 'Python'   "$t\python\python.exe" @('--version') 'Python 3\.\d+'
    Probar 'pip'      "$t\python\python.exe" @('-m', 'pip', '--version') 'pip \d+'
    Probar 'CMake'    "$t\cmake\bin\cmake.exe" @('--version') 'cmake version'
    Probar 'Ninja'    "$t\ninja\ninja.exe" @('--version') '^\d+\.\d+'
    Probar 'clang'    "$t\llvm-mingw\bin\clang.exe" @('--version') 'clang version'
    Probar 'Java'     "$t\jdk21\bin\java.exe" @('-version') 'version "21'
    Probar 'MAME'     "$t\mame\mame.exe" @('-version') '^\d+\.\d+'
    Probar 'Git'      "$t\git\cmd\git.exe" @('--version') 'git version'

    # imhex.exe es la CLI (imhex-gui.exe es la interfaz); --version no abre ventanas.
    Probar 'ImHex'    "$t\imhex\imhex.exe" @('--version') 'Version:\s+\d+\.\d+'
    Probar 'ImHex portable' "$t\imhex\imhex.exe" @('--version') 'Type:\s+Portable'

    # --- SDL3 "hola mundo": CMake + Ninja + clang + recursos (.rc), todo bajo la ruta con espacio.
    $srcSdl  = Join-Path $Entorno 'prueba_sdl3'
    $buildSdl = Join-Path $Verif 'build sdl3'
    if (Test-Path -LiteralPath $buildSdl) { Remove-Item -LiteralPath $buildSdl -Recurse -Force }
    $cmake = "$t\cmake\bin\cmake.exe"
    $r = Invoke-Proceso -Exe $cmake -Argumentos @('-S', $srcSdl, '-B', $buildSdl, '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=clang', '-DCMAKE_RC_COMPILER=llvm-windres')
    Agregar 'SDL3: configurar (CMake)' ($r.Codigo -eq 0) $(if ($r.Codigo -eq 0) { 'cmake configuró en "' + $buildSdl + '"' } else { $r.Salida })
    if ($r.Codigo -eq 0) {
        $r = Invoke-Proceso -Exe $cmake -Argumentos @('--build', $buildSdl)
        Agregar 'SDL3: compilar (Ninja+clang+windres)' ($r.Codigo -eq 0) $(if ($r.Codigo -eq 0) { 'hola_sdl3.exe compilado' } else { $r.Salida })
        $exe = Join-Path $buildSdl 'hola_sdl3.exe'
        if ($r.Codigo -eq 0 -and (Test-Path -LiteralPath $exe)) {
            $r = Invoke-Proceso -Exe $exe -Argumentos @('2000') -Dir $buildSdl -TimeoutSeg 30
            Agregar 'SDL3: ventana' ($r.Codigo -eq 0 -and $r.Salida -match 'OK') $r.Salida.Replace("`r", '').Replace("`n", ' | ')
        }
    }

    # --- Ghidra headless: crea un proyecto e importa un PE dentro de la ruta con espacio.
    $proj = Join-Path $Verif 'proyecto ghidra'
    if (Test-Path -LiteralPath $proj) { Remove-Item -LiteralPath $proj -Recurse -Force }
    New-Item -ItemType Directory -Path $proj | Out-Null
    $muestra = Join-Path $buildSdl 'hola_sdl3.exe'
    if (-not (Test-Path -LiteralPath $muestra)) { $muestra = "$t\ninja\ninja.exe" }
    $r = Invoke-Proceso -Exe "$t\ghidra\support\analyzeHeadless.bat" -Argumentos @($proj, 'PruebaPD', '-import', $muestra, '-noanalysis', '-deleteProject') -TimeoutSeg 600
    $okG = ($r.Codigo -eq 0 -and $r.Salida -match 'Import succeeded')
    $detG = $r.Salida
    if ($okG) { $detG = 'analyzeHeadless importó ' + (Split-Path $muestra -Leaf) + ' (Import succeeded)' }
    Agregar 'Ghidra headless' $okG $detG
    Add-Content -LiteralPath $LogFile -Value ("--- salida completa de Ghidra ---`n" + $r.Salida) -Encoding UTF8

    return $res
}

# ================================================================ principal
try {
    Log "Projecto Decompile - instalador ($((Get-Date).ToString('s')))" 'Cyan'
    Log "Raíz:  $Root"
    Log "Log:   $LogFile"

    if ($CopiarRoms) { Copy-Roms; exit 0 }

    if (-not $SoloVerificar) {
        # 1. No se ejecuta como administrador.
        $id = [Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()
        if ($id.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
            Fallo 'Se está ejecutando como administrador. Cierra esta ventana y ejecuta INSTALAR.bat como usuario normal.'
        }
        # 2. Espacio libre.
        $unidad = (Get-Item -LiteralPath $Root).PSDrive
        $libreGB = [math]::Round($unidad.Free / 1GB, 1)
        Log "Espacio libre en $($unidad.Name): $libreGB GB (mínimo $($Cfg.espacio_minimo_gb) GB)"
        if ($libreGB -lt $Cfg.espacio_minimo_gb) { Fallo "Espacio insuficiente: $libreGB GB libres." }

        # 3. Herramientas (7-Zip primero: extrae todo lo demás).
        $resumen = New-Object System.Collections.Generic.List[object]
        foreach ($h in $Cfg.herramientas) {
            $acc = Install-Herramienta $h
            $resumen.Add([pscustomobject]@{ Herramienta = $h.nombre; Version = $h.version; Accion = $acc })
        }
        Log ''
        $resumen | Format-Table -AutoSize | Out-String | ForEach-Object { Log $_ }

        # 4. Raíz y repositorio.
        Sync-Raiz
        Initialize-Repo
    }

    # 5. Verificación.
    $res = Test-Entorno
    Write-Host ''
    $tabla = $res | Format-Table Prueba, Estado, Detalle -AutoSize -Wrap | Out-String -Width 160
    Write-Host $tabla
    Add-Content -LiteralPath $LogFile -Value $tabla -Encoding UTF8
    $fallas = @($res | Where-Object { $_.Estado -ne 'OK' }).Count
    if ($fallas -gt 0) { Log "$fallas prueba(s) con FALLA. Revisa $LogFile" 'Red' }
    else { Log 'Todas las pruebas OK.' 'Green' }

    # 6. ROMs (solo con confirmación).
    if (-not $SoloVerificar -and -not $SinRoms) { Copy-Roms }

    Log ''
    Log 'Para activar el entorno en una sesión:' 'Cyan'
    Log "  PowerShell:  . `"$Root\env.ps1`""
    Log "  cmd:         call `"$Root\env.bat`""
    if ($fallas -gt 0) { exit 2 } else { exit 0 }
}
catch {
    Log ("Instalación detenida: " + $_.Exception.Message) 'Red'
    exit 1
}
