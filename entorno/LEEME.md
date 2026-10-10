# Projecto Decompile: entorno portable

Todo el entorno vive en una sola carpeta (`C:\Projecto Decompile`). No usa permisos de
administrador, no modifica el PATH global y no escribe en el registro.

```
Projecto Decompile\
  INSTALAR.bat        único punto de entrada del instalador
  env.ps1 / env.bat   activan el entorno SOLO en la sesión actual
  LEEME.md            este archivo
  tools\              herramientas portables (+ _descargas, _cache, _verificacion)
  repo\               código del port (git). El instalador vive en repo\entorno\
  roms\               copia de TUS ROMs (fuera del repo, nunca se versiona)
  logs\               bitácoras de instalación
```

## Instalar o reparar

Doble clic en `INSTALAR.bat`, o desde una consola:

```
"C:\Projecto Decompile\INSTALAR.bat"
```

- Descarga solo de las fuentes oficiales listadas en `repo\entorno\versions.json` y verifica
  el hash de cada archivo. Si alguno no coincide, se detiene y lo reporta.
- Es idempotente: salta lo que ya está instalado, repara lo que falte o esté roto y reinstala
  solo lo que cambió de versión.
- Al final ejecuta una verificación (versiones, un "hola mundo" con SDL3 que abre una ventana
  unos 2 segundos y una importación con Ghidra headless) y muestra una tabla OK/FALLA.
- Después pregunta si quieres copiar los ROMs desde GOG a `roms\`. La respuesta por defecto
  es **No**.

Opciones:

| Opción | Efecto |
|---|---|
| `-SoloVerificar` | No instala nada; solo ejecuta la verificación. |
| `-CopiarRoms` | Solo copia los ROMs (pide confirmación). |
| `-SinRoms` | No pregunta por los ROMs al final. |
| `-Gog "ruta"` | Ruta de la instalación de GOG (por defecto `K:\Metal Slug X`). |

La bitácora de cada ejecución queda en `logs\instalacion_AAAAMMDD_HHMMSS.log`.

## Activar el entorno

PowerShell (en una sesión abierta; fíjate en el punto inicial):

```
. "C:\Projecto Decompile\env.ps1"
```

O bien abre una sesión ya activada:

```
powershell -NoExit -ExecutionPolicy Bypass -File "C:\Projecto Decompile\env.ps1"
```

cmd:

```
call "C:\Projecto Decompile\env.bat"
```

El entorno antepone al PATH de la sesión: python, cmake, ninja, llvm-mingw (clang), SDL3 (bin),
jdk21, ghidra\support (analyzeHeadless), mame, 7-Zip, imhex y git. También define `JAVA_HOME`,
`GHIDRA_INSTALL_DIR`, `CMAKE_PREFIX_PATH` (SDL3), `CMAKE_GENERATOR=Ninja`, `CC=clang`,
`PIP_CACHE_DIR` (dentro de tools\) y `PD_ROOT`, `PD_TOOLS`, `PD_REPO` y `PD_ROMS`.
El prompt muestra `(PD)`.

## Actualizar una herramienta

1. Edita `repo\entorno\versions.json`: cambia `version`, `url` y `hash` de la herramienta.
   Toma el hash de la fuente oficial que indica `fuente_hash` (digest de la release en GitHub,
   archivo SHA-256 de CMake, API de Adoptium, catálogo de NuGet).
2. Vuelve a ejecutar `INSTALAR.bat`. Solo se reinstala la herramienta que cambió.

Aviso: al actualizar Python se reinstala `tools\python` completo y se pierden los paquetes
instalados con pip. Habrá que reinstalarlos desde el `requirements.txt` del repo.

## Desinstalar

Borra la carpeta `C:\Projecto Decompile`. No queda nada en el registro ni en el PATH global.
La configuración de Ghidra, la caché de pip y la configuración de ImHex (modo portable) se
guardan dentro de `tools\`.

## Reglas del repositorio

- `repo\` nunca contiene ROMs, BIOS, gráficos, audio ni datos extraídos. El `.gitignore`
  funciona como lista blanca: ignora todo y solo deja pasar código y documentación.
- Los ROMs se leen en tiempo de ejecución desde `roms\` y se validan por hash.
