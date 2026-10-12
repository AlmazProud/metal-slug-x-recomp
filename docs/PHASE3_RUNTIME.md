# Fase 3: runtime de hardware

El port (`build/src/mslugx.exe`) arranca **el BIOS MVS del usuario recompilado**, pasa sus
pruebas y entra al juego: attract mode, moneda, START, "How to play", selección de soldado y la
Misión 1 jugable, **con sonido** (Fase 3B). Solo necesita `SDL3.dll`.

```
mslugx.exe --roms "K:\Metal Slug X\resources\game"
```

## Componentes

| Archivo | Qué modela |
|---|---|
| `src/neogeo/roms.c`, `sha256.c` | Carga los archivos de GOG/DotEmu y los valida por SHA-256. El repo solo contiene los hashes. |
| `src/neogeo/bus.c` | Mapa de memoria completo del 68000: vectores conmutables, P1, RAM, ventana y bancos de P2, protección ALTERA, E/S, latch del sistema, paleta con dos bancos, BIOS y SRAM con bloqueo. |
| `src/neogeo/video.c` | LSPC: VRAM con buffer de lectura y módulo, registros, autoanimación, lista de sprites por línea (hasta 96 de 381), sprites con reducción, encadenado, volteo y envoltura, capa fix (BIOS o cartucho), conversión de color por la red de resistencias, sombra. |
| `src/neogeo/machine.c` | Planificador por ciclos del 68000: líneas, VBlank (IRQ1), IRQ2 (contador de posición con sus tres modos de recarga), IRQ3 de encendido, watchdog y entrega de interrupciones. |
| `src/neogeo/rtc.c` | Reloj NEC uPD4990A en modo serie: comandos, registro de desplazamiento, TP y DATA OUT. |
| `src/neogeo/sound.c` | Z80 (z80.h, zlib) con su mapa (M1 con bancos NEO-ZMC, RAM, latch, NMI) y YM2610 (ymfm, BSD-3, vía `ym2610.cpp`), intercalado con el 68000 como el planificador de MAME (Fase 3B). |
| `src/port/main.c` | SDL3: ventana con escala entera, teclado y mando, ritmo de 59,1856 Hz, audio a 55 555 Hz (colchón de 50 ms), SRAM persistente y modo sin ventana para las pruebas (`--capturas`, `--guion`, `--wav`, `--traza`, `--traza-es`). |

**Decisión sobre el BIOS:** se usa el del usuario (`sp-u2`, recompilado junto con P1), no una
reimplementación (HLE). Ya está verificado en el lockstep, MAME (el oráculo) ejecuta el mismo
binario, y un HLE añadiría comportamiento propio sin forma de validarlo.

## Hallazgos verificados contra MAME

Herramientas: `tools/mame/screenshots.lua`, `trace_ports.lua`, `trace_sound.lua`,
`z80_latency.lua` y `trace_io.lua`, más `tools/compare_screenshots.py`. En el port, las
opciones `--capturas`, `--traza` y `--traza-es` producen los mismos formatos.

1. **Valores de los puertos en reposo** (`trace_ports.lua`):
   - `300001`: DIP = `FF`;
   - `300081`: TEST = `80` (bit 6 en 0 indica MVS);
   - `320001`: `1F`, más TP en el bit 6 y DATA OUT en el bit 7;
   - `340000` y `380000`: `FF` en el byte alto.
2. **El BIOS espera flancos de TP del reloj** (bucle en `C11D6A`). Sin el uPD4990A, el modelo
   mínimo de la Fase 2 se quedaba atascado ahí.
3. **El BIOS y el juego esperan respuestas del Z80** (en la 3A, antes del Z80 real, se usó un
   sustituto con estas latencias). El comando 1 se responde con 1 y el 3 con
   0; el resto no cambia la respuesta. Si la respuesta 1 no llega, el BIOS se detiene con el
   error 8 (`C11FE8`). Latencias medidas en el puerto `0x0C` del Z80:

   | Comando | Latencia | En ciclos del 68000 |
   |---|---|---|
   | 1 | 14 660,5–14 662 µs | 175 929 |
   | 3 | 60,4–61,5 µs | 729 |

   La lectura de `0x0C` hay que interceptarla con todos sus espejos de E/S (`OUT (C)` pone B en
   el byte alto de la dirección).
4. **Orden de píxeles de los sprites de DotEmu:** cada mitad de 8 píxeles está invertida
   respecto de la pantalla (`col ^ 7`). Es coherente con "píxel x = bit 7 − x" de
   `ROM_MAP.md`. Se detectó porque "FIRE" se veía espejado por mitades.
5. **Fase del cuadro:** en MAME, en el instante 0 el haz está al comienzo del VBlank
   (vpos 240), y el primer VBlank llega un cuadro completo después. Con el origen en vpos 0,
   todas las escrituras sincronizadas con el VBlank llegaban 24 líneas (1536 µs) antes. Por eso
   el cuadro del port termina en el VBlank y su número coincide con `screen:frame_number()`.
6. **Interrupciones en el límite de la instrucción:** el generador emite un punto de control
   antes de cada instrucción (`program.py`; se desactiva con `--sin-punto-por-instruccion`).
   Una interrupción entra antes de la primera instrucción que empieza en su ciclo o después,
   y no en el siguiente punto de despacho.

7. **Numeración de cuadros de MAME:** la captura que MAME etiqueta como cuadro N es la imagen
   que se completa al ejecutar el cuadro N (en el port, cuando `ng.cuadros` pasa de N a N + 1).
   Con la numeración anterior, desplazada en uno, solo coincidían 321 de 451 cuadros
   consecutivos; con esta coinciden 448. El guion de entradas usa la misma numeración.

## Fase 3B: sonido

- **Z80 + YM2610 reales** en lugar del sustituto, con el mismo núcleo YM que MAME (ymfm,
  fidelidad media, 55 555 Hz) y su mezcla (SSG ×0,84; FM izquierda/derecha ×0,98).
- **Intercalado como MAME:**
  - el Z80 avanza hasta cada límite de tramo del 68000: inicio de línea, VBlank, recarga de
    IRQ2, temporizadores del YM;
  - cada comando sincroniza en el acto y abre 50 µs de intercalado fino (perfect quantum);
  - fuera de esa ventana, el 68000 ve la respuesta en el tramo siguiente.
- **Línea de tiempo contra MAME** (`--traza` frente a `z80_latency.lua`): comandos,
  respuestas del Z80 y el momento en que el 68000 las ve coinciden a pocos microsegundos.

  | Evento | MAME | Port |
  |---|---|---|
  | Respuesta 01 vista por el 68000 | 4 684 418,9 µs | 4 684 418,8 µs |
  | Respuesta 00 del Z80 tras CMD 03 | 4 736 282,0 µs | 4 736 282,0 µs |
  | E1 en el cuadro 879 | 14 868 559,1 µs | 14 868 558,5 µs |

  Esto se mantiene durante los primeros 18 comandos (hasta el cuadro 880).
- **Audio contra `-wavwrite` de MAME** (`tools/compare_audio.py`, MAME con
  `-samplerate 55555`):
  - en los tramos con la misma secuencia de comandos, la correlación es **1,00000** con un
    desfase fijo de 6 muestras (latencia del flujo de MAME);
  - desde el cuadro ~919 el juego dispara los sonidos un cuadro antes que MAME (desfase de
    990 muestras): es la deriva de la lógica del 68000 que se describe abajo, no del sonido.
- **En ventana:** cola media de 32 ms, sin cortes ni descartes en 20 s (colchón inicial de 50 ms).
- **Rendimiento:** unos 1600 cuadros/s sin ventana (27 veces el tiempo real); con ventana,
  ~12 % de un núcleo.

## Resultados

- **Colores y render, píxel a píxel contra MAME:**
  - de 451 cuadros consecutivos (250–700), **448 son idénticos**; los otros 3 difieren en
    1-18 píxeles (chispas del logo);
  - en los cuadros 100, 200, 300, 400, 500, 600, 900, 1200, 1500 y 1800, 8 de 10 son
    idénticos. La demo del attract mode se desvía antes del 1500 (deriva de ciclos, abajo).
- **Temporización:**
  - el primer comando al Z80 (4,67 s después del encendido) cae a 1,25 µs del de MAME;
  - en la ventana de los cuadros 280–282, las 4436 escrituras de E/S coinciden en orden,
    dirección y dato, con un desfase constante de 1,1 µs.
- **Partida simulada** (`--guion`): moneda, START, "How to play", selección de soldado y la
  Misión 1 jugable, con disparos, puntuación y enemigos.
- **Rendimiento:** sin sonido (3A), unos 3500 cuadros/s; con el Z80 y el YM2610 (3B), unos
  1600 cuadros/s sin ventana. Con ventana usa alrededor del 12 % de un núcleo.
- **Accesos:** 0 lecturas y 0 escrituras sin mapear; el watchdog nunca reinicia la máquina.
- **Pruebas (`ctest`): 3/3.**
  - Los dos lockstep ahora ponen además un punto de control cada ~1000 ciclos: 20 097 puntos
    desde el reset y 304 458 desde la partida.

## Diferencias conocidas con MAME (para la Fase 4)

> **Resueltas en la Fase 4** (`docs/PHASE4_VERIFICATION.md`): el port reproduce la Misión 1 sin
> divergencias de RAM ni de imagen. Se conserva la lista como registro de lo que se encontró.

1. **Orden de las escrituras de 32 bits.** Con `MOVE.L` a `-(An)`, el 68000 (y MAME) escribe
   primero la palabra baja; el port escribe siempre primero la alta. El valor final es el
   mismo, pero el orden importa en registros de E/S. Se observó en las escrituras de paleta.
2. **Deriva de ciclos del 68000.**
   - Es de unos 0,5 µs por cuadro: los tiempos por instrucción vienen de la tabla de Musashi,
     y MAME usa su núcleo 68000 de ciclo exacto.
   - Las escrituras de 32 bits se registran en el mismo ciclo; en el hardware van separadas
     por 4 ciclos.
3. **Efecto visible.** Hacia el cuadro 900 del attract, el juego hace algo un cuadro antes que
   MAME (el comando E5 en el cuadro 919, y luego la demo). Es la siguiente divergencia a
   localizar en la Fase 4 con la comparación de RAM por cuadro.
4. **Reloj.** El BIOS muestra la hora; el port usa la del sistema o `--fecha`, y MAME la del
   sistema.
5. **Lecturas sin mapear.** Devuelven `0xFFFF`; en el hardware real devuelven el último dato
   del bus (el prefetch). No se observó ninguna.
