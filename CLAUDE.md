# CLAUDE.md: Projecto Decompile (port nativo de Metal Slug X)

Port nativo para Windows de Metal Slug X (Neo Geo MVS, 1999) por **recompilación estática
progresiva**: el código 68000 se traduce a C, el hardware Neo Geo se reimplementa como runtime
nativo y después las funciones recompiladas se reemplazan poco a poco por C legible.

## Reglas obligatorias

1. **Trae tu propio ROM.** El repo nunca contiene ROMs, BIOS, gráficos, audio ni datos
   extraídos. Se leen de `..\roms\` en tiempo de ejecución y se validan por hash. El
   `.gitignore` es una lista blanca: ignora todo y solo deja pasar código y documentación.
2. **MAME solo como referencia de documentación del hardware.** No se copia código de MAME.
   Se prefieren bibliotecas con licencia permisiva (por ejemplo Musashi o ymfm), cada una
   documentada en `THIRD_PARTY.md`.
3. **MAME es el oráculo.** Todo comportamiento se valida contra MAME con trazas o comparación
   de estado. No se supone nada sin verificarlo.
4. **Stack:** C17, CMake, Ninja, llvm-mingw y SDL3, todo tomado de `..\tools`. Las herramientas
   de análisis se escriben en Python 3.
5. Este archivo se actualiza al cierre de cada fase.

**Forma de trabajar:** una fase a la vez. Antes de escribir código se presenta un plan corto
y se espera el OK. Al cerrar cada fase se reporta qué funciona, qué falta, los riesgos y el
siguiente paso, y se espera el visto bueno. Commits pequeños con mensajes claros. Toda
incertidumbre (formato, protección, timing) se investiga y se verifica contra MAME.

## Estructura

```
C:\Projecto Decompile\
  INSTALAR.bat, env.ps1, env.bat, LEEME.md   (copias de repo\entorno\)
  tools\   herramientas portables       roms\  ROMs del usuario (fuera del repo)
  logs\    bitácoras                    repo\  este repositorio
repo\
  entorno\   instalador (setup.ps1, versions.json, env.*, prueba_sdl3\)
```

Activar el entorno: `. "C:\Projecto Decompile\env.ps1"` (PowerShell) o
`call "C:\Projecto Decompile\env.bat"` (cmd).

## Estado por fase

| Fase | Estado |
|---|---|
| Entorno | **Cerrada** (2026-10-10). Verificación 16/16 OK. ROMs copiados a `roms\`. |
| 0. Reconocimiento | **Cerrada** (2026-10-11), pendiente del visto bueno. Ver `docs/ROM_MAP.md`. |
| 1. Análisis 68000 | No iniciada |
| 2. Recompilador | No iniciada |
| 3. Runtime | No iniciada |
| 4. Arranque y verificación diferencial | No iniciada |
| 5. Mejoras nativas | No iniciada |

### Fase 0: resultado

- **Extractor:** `herramientas/extraer_roms.py` reconstruye el set de MAME en `roms\mame\`
  a partir de los archivos de DotEmu. 15 archivos coinciden exactos (CRC32 + SHA1) con
  `mame -listxml`; sigue siendo MAME quien da los hashes.
- **P1 de GOG:** es distinto del de MAME (CRC `60c33b1a`). El origen no está identificado y se
  investigará en la Fase 1. El port y el oráculo usan el mismo P1, el de GOG.
- **`sm1.sm1`:** GOG no lo incluye. Se sustituye con M1, como arranca la AES. Verificado:
  arranca, entra en attract mode y el sonido funciona.
- **Formatos de DotEmu:** fix y sprites vienen desempaquetados a 4 bits por píxel en orden de
  filas. La conversión es reversible y está documentada.
- **Protección:** ALTERA en `0x2FFFE0`–`0x2FFFEF`, que devuelve bits de una tabla en P1
  (`0x0DEDD2`). Confirmada con `herramientas/mame/traza_proteccion.lua`: 127 206 de 127 206
  lecturas coinciden con el modelo.
- **Arrancar el oráculo:** `mame mslugx -bios us -rompath "<raíz>\roms\mame"`.
- **Hook pre-commit:** rechaza binarios, archivos de más de 1 MB y copias de ROMs.

### Fase Entorno: resultado

Versiones fijadas en `entorno/versions.json` (todas con hash verificado de la fuente oficial):

| Herramienta | Versión |
|---|---|
| 7-Zip | 26.04 |
| Python | 3.14.8 (NuGet) |
| CMake | 4.4.4 |
| Ninja | 1.13.2 |
| llvm-mingw | 20261006 (clang 23.1.3, UCRT) |
| SDL3 | 3.4.18 |
| Temurin JDK | 21.0.12.1 |
| Ghidra | 12.1.4 |
| MAME | 0.289 |
| ImHex | 1.38.1 |
| MinGit | 2.56.0.2 |

Verificado bajo la ruta con espacio: CMake, Ninja, clang, llvm-windres y SDL3 (compila y abre
la ventana), y Ghidra headless (importa un PE). Ghidra no deja nada en `%APPDATA%`,
`%LOCALAPPDATA%` ni `%TEMP%`.

ROMs: 13 archivos `mslugx_*` copiados desde GOG a `roms\`, con `roms\MANIFEST.sha256`
verificado.

## Decisiones

- **Python desde NuGet** en lugar del embeddable: trae pip y venv sin parches. Python 3.14
  porque la 3.15 era demasiado reciente.
- **El instalador se versiona en `repo/entorno/`.** `INSTALAR.bat` de la raíz lo llama desde
  ahí y la raíz se calcula como el padre de `repo`.
- **MinGit portable** en lugar del Git del sistema, para que todo quede en una sola carpeta.
- **Llamar `.bat` con rutas que tienen espacio:** se usa `cmd /d /s /c "..."` (función
  `Invoke-Proceso` de setup.ps1). Si no, cmd elimina las comillas exteriores y la ruta se rompe.
- **El hash de Python es SHA512**, porque NuGet solo publica ese. El resto usa SHA256.
- **Identidad de git** configurada solo en este repo (`git config --local`).
- **Ghidra portable mediante la ruta corta 8.3** (opción B). `setup.ps1` escribe en
  `launch.properties` las propiedades `application.settingsdir`, `cachedir`, `tempdir` y
  `java.io.tmpdir` apuntando a `C:/PROJEC~1/tools/_cache/ghidra/...`. La ruta corta se
  recalcula en cada ejecución. Si la unidad no tiene nombres 8.3, el instalador avisa y Ghidra
  usa sus rutas por defecto. Se descartó poner comillas (opción A) porque `LaunchSupport` las
  toma literalmente.
- **Archivos de texto:** los `.bat` y `.ps1` usan CRLF (fijado en `.gitattributes`). Los
  `.ps1` llevan BOM UTF-8, porque PowerShell 5.1 los lee como ANSI si no lo tienen.

## Hallazgos

- **Instalación GOG** en `K:\Metal Slug X`. No trae un set de MAME: son archivos de DotEmu en
  `resources\game\` (`mslugx_game_m68k` 5 MB, `mslugx_tiles` 48 MB, `mslugx_adpcm` 10 MB,
  `mslugx_game_z80`, `mslugx_game_sfix`, `mslugx_bios_m68k`, `mslugx_bios_sfix`,
  `mslugx_zoom_table`, entre otros). Lo más probable es que no coincidan byte a byte con el
  set `mslugx` de MAME y que haya que reconstruir el formato de MAME en la Fase 0.
- **Ghidra y los espacios:** con su configuración original, Ghidra 12.1.4 funciona bajo una
  ruta con espacio. Lo que falla es que `launch.bat` (línea 185) concatena los `VMARGS` de
  `launch.properties` sin comillas, y `LaunchSupport` toma las comillas literalmente. Por eso
  no se puede redirigir `application.settingsdir`, `cachedir` ni `tmpdir` a una ruta con espacio.
  Ghidra sí respeta `application.settingsdir` también para `java_home.save`.

## Pendientes

- [ ] **Fase 1:** plan corto y esperar el OK.
- [ ] Identificar en qué difiere el P1 de GOG del de MAME (Fase 1).
- [ ] Lua en MAME: las suscripciones (`add_machine_stop_notifier`, taps) deben guardarse en
      variables globales; si no, Lua las recolecta en silencio.
