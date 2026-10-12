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
  entorno\      instalador (setup.ps1, versions.json, env.*, prueba_sdl3\)
  src\cpu\      despachador, estado del 68000, respaldo con Musashi
  src\neogeo\   runtime de hardware (bus, LSPC, RTC, planificador, ROMs)
  src\port\     ejecutable SDL3 (mslugx.exe)
  herramientas\ análisis, recompilador (recomp68k\), scripts de MAME (mame\), comparación
  tests\        diferencial (instrucciones) y lockstep (programa completo)
  externo\      Musashi
```

Compilar: `cmake -S repo -B repo\build -G Ninja -DCMAKE_BUILD_TYPE=Release` y
`cmake --build repo\build`. El programa recompilado se genera en `build\src\programa\` a
partir de `roms\` y `analisis\`. Jugar:
`repo\build\src\mslugx.exe --roms "K:\Metal Slug X\resources\game"`.

Activar el entorno: `. "C:\Projecto Decompile\env.ps1"` (PowerShell) o
`call "C:\Projecto Decompile\env.bat"` (cmd).

## Estado por fase

| Fase | Estado |
|---|---|
| Entorno | **Cerrada** (2026-10-10). Verificación 16/16 OK. ROMs copiados a `roms\`. |
| 0. Reconocimiento | **Cerrada** (2026-10-11). Ver `docs/ROM_MAP.md`. |
| 1. Análisis 68000 | **Cerrada** (2026-10-11). Ver `docs/FASE1_COBERTURA.md`. |
| 2. Recompilador | **Cerrada** (2026-10-11). `ctest`: 3/3. |
| 3A. Runtime: imagen y controles | **Cerrada** (2026-10-11). Ver `docs/FASE3_RUNTIME.md`. |
| 3B. Runtime: sonido (Z80 + YM2610) | **Cerrada** (2026-10-11), pendiente del visto bueno. Ver `docs/FASE3_RUNTIME.md`. |
| 4. Arranque y verificación diferencial | No iniciada |
| 5. Mejoras nativas | No iniciada |

### Fase 3B: resultado

- **Sonido real:** Z80 (`externo/chips/z80.h`, zlib) + YM2610 (`externo/ymfm`, BSD-3, el mismo
  núcleo que MAME) en `src/neogeo/sonido.c`; `ym2610.cpp` es el único C++ del port (runtime de
  C++ estático: el ejecutable solo necesita `SDL3.dll`). Audio por SDL3 a 55 555 Hz, colchón
  de 50 ms, sin cortes.
- **Intercalado como el planificador de MAME:** el Z80 avanza hasta cada límite de tramo del
  68000; un comando sincroniza en el acto y abre 50 µs de intercalado fino. Comandos,
  respuestas y lecturas coinciden con MAME a pocos µs hasta el cuadro 880.
- **Audio contra `-wavwrite`:** correlación 1,00000 por tramo (`herramientas/comparar_audio.py`).
- **Numeración de cuadros corregida:** la captura N de MAME es la imagen que se completa al
  ejecutar el cuadro N. Así coinciden 448 de 451 cuadros consecutivos (antes, 321).

### Fase 3A: resultado

Detalle y evidencia en `docs/FASE3_RUNTIME.md`.

- **Arranque:** el port ejecuta el BIOS MVS del usuario recompilado (no HLE), entra al juego y
  la Misión 1 es jugable con teclado o mando (sonido: Fase 3B).
- **Runtime (`src/neogeo/`):** mapa de memoria completo, protección, LSPC (sprites, fix,
  reducción, paletas, autoanimación, IRQ1/IRQ2/IRQ3), uPD4990A, watchdog y SRAM persistente.
  Los ROMs se leen de la carpeta de GOG y se validan por SHA-256.
- **Contra MAME:**
  - capturas idénticas píxel a píxel, colores incluidos (cifras actuales en la Fase 3B);
  - las escrituras de E/S coinciden en orden y dato, con un desfase de unos 1 µs.
- **Interrupciones en el límite de la instrucción:** el generador pone un punto de control
  antes de cada instrucción. El despachador usa `rc_irq_nivel` para volver a `rc_evento`
  cuando la máscara baja.
- **Fase del cuadro como en MAME:** al encender, el haz está en vpos 240; el cuadro termina en
  el VBlank.
- **Sustituto del Z80** con latencias medidas (comando 1: 175 929 ciclos, comando 3: 729);
  reemplazado en la 3B.
- **Herramientas:**
  - `comparar_capturas.py`, `imagenes.py` (PNG/BMP sin dependencias);
  - en `mame/`: `capturas.lua`, `traza_puertos.lua`, `traza_sonido.lua`, `latencia_z80.lua` y
    `traza_es.lua`;
  - en el port: `--sin-ventana`, `--capturas`, `--guion`, `--traza` y `--traza-es`.

### Fase 2: resultado

- **Musashi 4.60 (MIT) sin SoftFloat** (opción B del usuario): `externo/musashi/` con
  sustitutos propios de `m68kfpu.c` y `softfloat/*.h`. Es la referencia de semántica y el
  intérprete de respaldo.
- **Decodificador propio** (`herramientas/recomp68k/decodificador.py`): Capstone decodifica
  formatos del 68020 en modo 68000. Validado en 116 581 instrucciones; las 6 diferencias son
  errores de Capstone.
- **Generador** (`generador.py`): las 87 instrucciones que usa el juego, con ciclos del 68000
  (tabla base de Musashi, volcada al compilar, más los ajustes dinámicos).
- **Programa completo** (`programa.py`): P1 y BIOS del usuario agrupados en regiones
  (componentes conexos del grafo de control), cada instrucción emitida una sola vez y entrada
  por `switch(c->pc)`. 124 193 instrucciones, 3 318 regiones, 31,7 MB de C en `build/`.
- **Despachador** (`src/cpu/despachador.c`): JSR/BSR = llamada nativa verificada
  (`rc_llamar`); puntos de control en los saltos hacia atrás; respaldo con Musashi en RAM, P2 y
  código no visto.
- **Pruebas** (`ctest`):
  - `diferencial_cpu`: 4 253 instrucciones reales (3 356 opcodes) × 64 estados = 272 192 casos,
    0 diferencias en registros, SR, PC, memoria y ciclos. Una prueba de mutación confirma que
    el arnés detecta errores.
  - `lockstep_reset`: programa completo contra Musashi desde el reset.
  - `lockstep_juego`: desde un estado real (volcado de MAME en el frame 4000 de
    `mision1.inp`): 120M ciclos, 591 VBlank, 10,1M instrucciones, memoria idéntica, un solo paso
    interpretado. El programa recompilado es unas 5 veces más rápido que Musashi.
- **Grabación del usuario:** `analisis/inp/mision1.inp`, 11 969 frames, hasta la Esfinge.
  Se reproduce determinista (`herramientas/mame/estado_final.lua`); para reproducirla se usa
  siempre una nvram vacía. Cobertura con ella: 40 404 PCs de P1.

### Fase 1: resultado

- **Herramientas:**
  - `herramientas/cobertura.py` + `mame/cobertura.lua`: cobertura dinámica en MAME. Usa la
    traza del depurador por partes, un bot de entrada, los cambios de banco y las lecturas de
    datos.
  - `herramientas/analisis68k.py`: desensamblado recursivo con Capstone, heurísticas A/B,
    validación contra MAME y comparación con Ghidra.
  - `herramientas/ghidra/*.java`: siembra de raíces y exportación de funciones en headless.
- **Mapa de memoria:**
  - P1 está fijo en `0x000000`.
  - P2 se ve por bancos de 1 MB en `0x200000–0x2FFFFF`; se cambia con una escritura en
    `0x2FFFF0` (banco = dato & 7). En la traza se usan los bancos 0–3.
- **Entradas:**
  - VBlank (IRQ1) en `0x22AC`, timer (IRQ2) en `0x2322`.
  - USER `0x2432`, PLAYER_START `0x2658`, DEMO_END `0x2800`, COIN_SOUND `0x27F6`.
  - Las excepciones del 68000 van a `0x22A2` (salto por `$10.w`).
- **Resultado (traza de 300 s):**
  - 26 406 direcciones de P1 ejecutadas.
  - 4 488 funciones; 46 % de P1 es código (12,6 % confirmado y el resto heurístico).
  - Todo lo que MAME ejecutó lo alcanza el análisis.
- **Precisión:** 130 bytes heurísticos sobre datos demostrados (heurística A). Ghidra, en
  comparación, encuentra el 55,5 % de lo ejecutado sin MAME; el análisis propio, el 51,3 %.
- **Código fuera de P1:**
  - 2 instrucciones en P2 (banco 1, `0x299178`, llamado desde `0x44EC`);
  - una rutina copiada a RAM (`0x10FC00`, `jmp $7C00(a5)` en `0x10EA`).
  - Implicación para la Fase 2: el dispatcher debe considerar el banco activo y recurrir al
    intérprete en RAM y en código no visto.

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

- [ ] **Fase 4:** plan corto (comparación de RAM y framebuffer por cuadro contra MAME) y
      esperar el OK. Primera divergencia conocida: hacia el cuadro 900 del attract (comando E5
      un cuadro antes que MAME).
- [ ] **Orden de las escrituras de 32 bits:** `MOVE.L` a `-(An)` escribe primero la palabra baja
      en el 68000 y en MAME; el port, siempre la alta.
- [ ] **Límites de tramo de MAME que el port aún no usa** para el intercalado con el Z80: por
      ejemplo, los temporizadores del uPD4990A.
- [ ] **Deriva de ciclos del 68000 contra MAME** (~0,5 µs por cuadro): tabla de Musashi frente
      al núcleo de ciclo exacto de MAME. Las escrituras de 32 bits van hoy en el mismo ciclo.
- [ ] **Relación de aspecto** de los píxeles (hoy cuadrados) y filtros: Fase 5.
- [ ] **Flags indefinidos y ciclos que siguen a Musashi:** N/V de ABCD/SBCD, N/Z en el
      desbordamiento de DIVU/DIVS y ciclos fijos de DIVU/DIVS. Hay que confirmarlos contra MAME
      (núcleo 68000 exacto).
- [ ] **DIVS 0x80000000/-1:** se implementa como el hardware (V=1); Musashi devuelve 0. Si
      aparece en una prueba, es una diferencia conocida.
- [ ] **Código de P2:** banco 1, 144 PCs; hoy lo ejecuta el intérprete. Recompilarlo por
      banco si el rendimiento lo pide.
- [ ] Descartar raíces de la heurística A cuyo código pise datos demostrados por MAME
      (hoy: 130 bytes).
- [ ] Ampliar la cobertura: el bot se estanca a partir de unos 200 s (26 406 PCs). Mejor
      grabar entradas reales (`-record`) de una partida de la Misión 1.
- [ ] 99 saltos o llamadas indirectos sin destino observado (por ejemplo `0x22AA` y `0x2354`).
- [ ] Identificar en qué difiere el P1 de GOG del de MAME (Fase 1).
- [ ] Lua en MAME: las suscripciones (`add_machine_stop_notifier`, taps) deben guardarse en
      variables globales; si no, Lua las recolecta en silencio. No llamar `io` a una variable
      local: tapa la biblioteca `io` de Lua.
