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
| Entorno | En cierre. Falta resolver dónde guarda Ghidra su configuración (ver pendientes). |
| 0. Reconocimiento | No iniciada |
| 1. Análisis 68000 | No iniciada |
| 2. Recompilador | No iniciada |
| 3. Runtime | No iniciada |
| 4. Arranque y verificación diferencial | No iniciada |
| 5. Mejoras nativas | No iniciada |

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
la ventana), y Ghidra headless (importa un PE).

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

- [ ] **Ghidra:** decidir cómo mantener settings, cache y temp dentro de `tools\`. La opción
      con comillas resultó inviable. La siguiente candidata es una ruta corta 8.3
      (`C:\PROJEC~1\...`, calculada por el instalador en cada ejecución). Mientras tanto,
      Ghidra escribe en `%APPDATA%\ghidra`, `%LOCALAPPDATA%\ghidra` y `%TEMP%\ghidra`.
- [ ] **Copiar los ROMs** a `roms\` (`INSTALAR.bat -CopiarRoms`); requiere confirmación.
- [ ] **Fase 0:** plan corto y esperar el OK.
