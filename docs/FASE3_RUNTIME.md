# Fase 3A: runtime de hardware (imagen y controles)

El port (`build/src/mslugx.exe`) arranca **el BIOS MVS del usuario recompilado**, pasa sus
pruebas y entra al juego: attract mode, moneda, START, "How to play", selección de soldado y la
Misión 1 jugable. Todavía no tiene sonido; eso corresponde a la Fase 3B.

```
mslugx.exe --roms "K:\Metal Slug X\resources\game"
```

## Componentes

| Archivo | Qué modela |
|---|---|
| `src/neogeo/roms.c`, `sha256.c` | Carga los archivos de GOG/DotEmu y los valida por SHA-256. El repo solo contiene los hashes. |
| `src/neogeo/bus.c` | Mapa de memoria completo del 68000: vectores conmutables, P1, RAM, ventana y bancos de P2, protección ALTERA, E/S, latch del sistema, paleta con dos bancos, BIOS y SRAM con bloqueo. |
| `src/neogeo/video.c` | LSPC: VRAM con buffer de lectura y módulo, registros, autoanimación, lista de sprites por línea (hasta 96 de 381), sprites con reducción, encadenado, volteo y envoltura, capa fix (BIOS o cartucho), conversión de color por la red de resistencias, sombra. |
| `src/neogeo/maquina.c` | Planificador por ciclos del 68000: líneas, VBlank (IRQ1), IRQ2 (contador de posición con sus tres modos de recarga), IRQ3 de encendido, watchdog y entrega de interrupciones. |
| `src/neogeo/rtc.c` | Reloj NEC uPD4990A en modo serie: comandos, registro de desplazamiento, TP y DATA OUT. |
| `src/neogeo/sonido.c` | **Sustituto temporal del Z80** (ver más abajo). |
| `src/port/main.c` | SDL3: ventana con escala entera, teclado y mando, ritmo de 59,1856 Hz, SRAM persistente y modo sin ventana para las pruebas. |

**Decisión sobre el BIOS:** se usa el del usuario (`sp-u2`, recompilado junto con P1), no una
reimplementación (HLE). Ya está verificado en el lockstep, MAME (el oráculo) ejecuta el mismo
binario, y un HLE añadiría comportamiento propio sin forma de validarlo.

## Hallazgos verificados contra MAME

Herramientas: `herramientas/mame/capturas.lua`, `traza_puertos.lua`, `traza_sonido.lua`,
`latencia_z80.lua` y `traza_es.lua`, más `herramientas/comparar_capturas.py`. En el port, las
opciones `--capturas`, `--traza` y `--traza-es` producen los mismos formatos.

1. **Valores de los puertos en reposo** (`traza_puertos.lua`):
   - `300001`: DIP = `FF`;
   - `300081`: TEST = `80` (bit 6 en 0 indica MVS);
   - `320001`: `1F`, más TP en el bit 6 y DATA OUT en el bit 7;
   - `340000` y `380000`: `FF` en el byte alto.
2. **El BIOS espera flancos de TP del reloj** (bucle en `C11D6A`). Sin el uPD4990A, el modelo
   mínimo de la Fase 2 se quedaba atascado ahí.
3. **El BIOS y el juego esperan respuestas del Z80.** El comando 1 se responde con 1 y el 3 con
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
   antes de cada instrucción (`programa.py`; se desactiva con `--sin-punto-por-instruccion`).
   Una interrupción entra antes de la primera instrucción que empieza en su ciclo o después,
   y no en el siguiente punto de despacho.

## Resultados

- **Colores y render, píxel a píxel contra MAME:**
  - en los cuadros 100, 200, 300, 400, 500, 900, 1200, 1500 y 1800 del arranque con nvram
    vacía, 9 de 10 son **idénticos**, incluida la demo del attract mode;
  - de 451 cuadros consecutivos (250–700), 321 son idénticos.
- **Temporización:**
  - el primer comando al Z80 (4,67 s después del encendido) cae a 1,25 µs del de MAME;
  - en la ventana de los cuadros 280–282, las 4436 escrituras de E/S coinciden en orden,
    dirección y dato, con un desfase constante de 1,1 µs.
- **Partida simulada** (`--guion`): moneda, START, "How to play", selección de soldado y la
  Misión 1 jugable, con disparos, puntuación y enemigos.
- **Rendimiento:** unos 3500 cuadros/s sin ventana, unas 60 veces el tiempo real. Con ventana
  usa alrededor del 12 % de un núcleo.
- **Accesos:** 0 lecturas y 0 escrituras sin mapear; el watchdog nunca reinicia la máquina.
- **Pruebas (`ctest`): 3/3.**
  - Los dos lockstep ahora ponen además un punto de control cada ~1000 ciclos: 20 097 puntos
    desde el reset y 304 458 desde la partida.

## Diferencias conocidas con MAME (para la Fase 4)

1. **Visibilidad de la respuesta del Z80.**
   - En MAME, el 68000 ve la respuesta entre 15,8 y 33,9 µs después de que el Z80 la escribe.
     Es un efecto del planificador, que ejecuta cada CPU por tramos.
   - El port la ve de inmediato, así que tras los primeros comandos va unos 17 µs adelantado.
   - Con el Z80 real (3B) habrá que reproducir el intercalado de MAME.
2. **Deriva de ciclos del 68000.**
   - Es de unos 0,5 µs por cuadro: los tiempos por instrucción vienen de la tabla de Musashi,
     y MAME usa su núcleo 68000 de ciclo exacto.
   - Las escrituras de 32 bits se registran en el mismo ciclo; en el hardware van separadas
     por 4 ciclos.
3. **Efecto visible.** Esos microsegundos cambian en qué punto de un bucle cae el VBlank, y a
   veces el juego aplica un paso de fundido un cuadro después. Pasa, por ejemplo, en el aviso
   "PARENTAL ADVISORY" (cuadros impares 305–333) y en el tramo 590–700.
4. **Reloj.** El BIOS muestra la hora; el port usa la del sistema o `--fecha`, y MAME la del
   sistema.
5. **Lecturas sin mapear.** Devuelven `0xFFFF`; en el hardware real devuelven el último dato
   del bus (el prefetch). No se observó ninguna.
