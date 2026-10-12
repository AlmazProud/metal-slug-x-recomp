# Fase 6: "Remaster"

Mejoras opcionales sobre el port de la Fase 5. Todas se apagan desde el menú, y ninguna cambia la
emulación cuando está apagada: `ctest` (con la Misión 1 contra MAME) sigue pasando.

| Subfase | Contenido | State |
|---|---|---|
| 6A | Disparo automático y modo debug | Cerrada (2026-10-12) |
| 6B | Catálogo de sonidos, menú de música, voces y efectos; vibración del mando | Cerrada (2026-10-12) |
| 6C | Menú de inicio moderno con la fuente del juego | Cerrada (2026-10-12) |
| 6D | Pantalla ancha | Descartada por el usuario: le quita la esencia al juego |

## 6A: disparo automático

Mientras se mantiene, el botón A (disparo) se pulsa y se suelta solo. Se configura por jugador en
el menú o en `mslugx.ini`:

- `autodisparo1` / `autodisparo2`:
  - `boton`, por defecto: con un botón propio, la acción "Disparo automatico" de los controles;
    teclas B (jugador 1) y G (jugador 2), y RB en el mando;
  - `disparo`: el botón de disparo es siempre automático;
  - `no`.
- `autodisparo_por_segundo`: 10, 7.5 o 5.

**Medición** (`--guion` con el patrón, contando el comando de sonido `F7`, disparo de pistola,
en 5 s de juego):

| Patrón | Disparos |
|---|---|
| Botón mantenido | 1 |
| Cada 4 cuadros | 50 |
| Cada 6 cuadros | 50 |
| Cada 8 cuadros | 37 |
| Cada 12 cuadros | 25 |

El juego no dispara la pistola más de 10 veces por segundo, por eso el máximo útil es un periodo de
6 cuadros. Las armas automáticas (la ametralladora pesada) ya disparan solas al mantener el botón.

## 6A: modo debug

F3, o "Modo debug" en el menú. Panel arriba a la izquierda:

- **FPS** del anfitrión, cuadro emulado y turbo.
- **Lógica del juego en Hz:** al salir de la espera, el juego copia en `0x10008A`
  (`-$7F76(a5)`, instrucción en `0x3A42`) cuántas VBlanks duró el ciclo de lógica.
  - 2 = 30 Hz, lo normal;
  - 3 o 4 = 20 o 15 Hz, la ralentización (en rojo).

  Se verificó con la Misión 1: la distribución por VBlank coincide con la medición de la Fase 5
  (`PD_CONTAR_PC`).
- **Carga del 68000** (media y pico de medio segundo): proporción de inicios de línea en los que el
  68000 no estaba en el bucle de espera (`0x39B0`-`0x3A41`). Se usa la media porque la lógica
  corre cada 2 cuadros y la carga de un cuadro suelto alterna.
- **Cuadros perdidos** por la ralentización en los últimos 10 s.
- **Gráfico** de la carga por cuadro en los últimos 4 s, en rojo los cuadros de un ciclo lento.

Teclas del modo debug:

| Tecla | Acción |
|---|---|
| F6 | avanzar un cuadro (deja el juego en pausa; P continúa) |
| Ctrl+1 / Ctrl+2 | ocultar los sprites / la capa fix (HUD y textos) |
| Ctrl+3 | cajas de los sprites dibujados en el cuadro, sin las columnas de fondo que ocupan toda la altura |
| Ctrl+4 | paleta activa (4096 colores, 4 paletas por fila) |
| Ctrl+5 | RAM de trabajo en hexadecimal (RePág / AvPág / Inicio) |

**Implementación:**

- `NgDebug ng_dbg` (`neogeo.h`): mediciones fuera del estado de la máquina. Solo se calculan con
  `ng_dbg.activo`.
- Las direcciones propias de Metal Slug X están en `src/port/profile.h`, primer paso hacia un
  perfil de juego.

**Pruebas:**

- `PD_DEBUG_VISTAS=cajas,paleta,memoria` abre las vistas al activar el modo.
- Con `PD_PRUEBA_VENTANA`, la ventana corre sin esperar al reloj.

## 6B: el driver de sonido de Metal Slug X

El 68000 controla el sonido con comandos de un byte al Z80 (`ng_sound_command`). Se descubrió con
la traza de comandos de la Misión 1 (`--traza`) y catálogos sin el juego:

- **Modos:** los comandos `00`, `10` y `11` fijan un modo que queda activo, y el comando siguiente
  se interpreta en ese modo. El mismo número suena distinto en cada uno.
  - **`00`, música:** pistas `20`-`31`, 18 en total.
  - **`10`, efectos:** `20`-`FF`, 185 con sonido.
  - **`11`, voces:** `20`-`6E`, 61 con sonido; incluye algunos efectos digitalizados.
- **Sistema:**
  - `03` reinicia el driver (el juego envía `03 03 06` antes de cada música);
  - `01` es el cambio de ranura del BIOS (deja el driver esperando un `03`);
  - `0C` aparece antes de algunos efectos;
  - `02` y `0F` suenan en cualquier modo.
- **Fuera de rango:** el driver lee fuera de sus tablas y suena basura.

**Catálogo** (`--catalogo-sonido` con `PD_CATALOGO_PREFIJO=MM`, desde el cuadro 1300, justo
después de que el juego reinicia el driver):

- para cada comando: duración, si queda en bucle, pico y notas iniciadas por tipo de canal
  (`ng_notes`: FM, SSG, ADPCM-A, ADPCM-B);
- las músicas reales usan FM y ADPCM a la vez y quedan en bucle.

`tools/sound_catalog.py` genera con eso `src/port/profile_sounds.c`: comando, duración y
nombre, sin datos de audio.

**Comprobación:** desde la mitad de la Misión 1, la secuencia `03 03 06` + modo + comando da las
mismas duraciones que el catálogo limpio, en los tres modos.

### Nombres

Solo se nombraron los sonidos confirmados con capturas del momento en que suenan en la partida
grabada; el resto se identifica por número:

- **Músicas:**
  - `20` fanfarria de misión cumplida;
  - `21` Misión 1;
  - `22` Misión 2 (la que suena tras completar la Misión 1);
  - `2C` combate contra el tanque;
  - `2E` "How to play";
  - `30` selección de soldado.
- **Voces:**
  - `31` "Mission 1... start";
  - `37` "Mission complete";
  - `49` selección de soldado;
  - `50` "Great!" (prisioneros rescatados).
- **Efectos:**
  - `F7` pistola: 50 disparos en 5 s con el autodisparo;
  - `8C` vulcan del Slug;
  - `84` cañón del Slug;
  - `22`, `25`, `21`, `A3` y `F5` explosiones;
  - `F2` impacto en blindaje.

El demo del attract mode no tiene música (la MVS lo deja sin sonido), así que más nombres
necesitan grabaciones de otras misiones.

## 6B: menú de música, efectos y voces

Menú → "Musica y sonidos":

- tres pestañas (← / →, o LB / RB): Música, Efectos y Voces;
- Enter / A escucha, Supr / Y detiene, Esc / B vuelve al juego.

**Funcionamiento** (`src/port/sounds.c`):

- al abrir se guarda el estado completo del juego y el 68000 queda detenido;
- el Z80 y el YM2610 avanzan solos (`ng_sonido_solo_*`);
- cada sonido se reproduce como lo hace el juego: `03 03 06`, el modo y el comando, un cuadro
  entre cada uno;
- al cerrar se restaura el estado y el juego sigue exactamente donde estaba.

**Test:** `ctest` → `menu_sonido` (`--probar-sonidos`).

- La música y la voz suenan: energía media de 2473 y 620, frente a 66 en silencio.
- Al cerrar, 300 cuadros del juego son idénticos a no haber abierto el menú (RAM, imagen y audio).

## 6B: vibración

`ng_on_sound_command` avisa al port de cada comando. El port sigue el modo del driver y, para los
efectos identificados, pide una vibración del mando:

| Efecto | Vibración |
|---|---|
| Explosión grande | Fuerte, 380 ms |
| Impacto o explosión | Fuerte a media, 200-300 ms |
| Cañón del Slug | Media, 140 ms |
| Vulcan, ráfaga y pistola | Leve, 35-45 ms |

- Dentro de un cuadro gana la más fuerte, y una vibración fuerte en curso no se corta por una leve.
- Vibran los dos mandos, porque el juego no indica de qué jugador es cada sonido.
- Intensidad: menú "Vibracion", o `vibracion=0-100` en `mslugx.ini` (70 por defecto; 0 la apaga).

## 6C: pantalla de inicio y menús

**Fuente** (`src/port/font.c`): la fuente sombreada 8x8 del propio juego, leída de la copia del
usuario.

- Está en la capa fix del cartucho, tiles `0x300` + código ASCII (la de "CREDIT").
- Índices: 1 contorno, 2-5 degradé, 7 sombra y 15 brillo. La paleta se dedujo comparando una
  captura con los tiles (`CREDIT 0`).
- Los colores son del port: plata, oro, gris y verde.
- Se dibuja en mayúsculas, porque sus minúsculas son muy bajas y se confunden.
- En `0x20` + ASCII hay otra fuente plana, sin usar.

**Menús** (`src/port/menu.c`): páginas apiladas, sobre el juego oscurecido. La escala es entera,
con un lienzo de al menos 440 x 250.

- **Inicio**, al abrir el juego (se apaga con `start_screen=0`): Jugar, Cargar partida,
  Configuración, Música y sonidos, Extras, Salir.
  - De fondo sigue corriendo el demo, sin recibir las entradas.
  - "Jugar" inserta una moneda y pulsa START como un jugador. Comprobado: CREDIT 1, y luego el
    juego empieza ("How to play", música `2E`).
- **Pausa** (Esc, o Guide / Back + Start): Continuar, Guardar y Cargar partida (9 ranuras),
  Configuración, Música y sonidos, Extras, Volver al inicio (reinicia la máquina conservando la
  SRAM), Salir.
- **Configuración:**
  - Video: pantalla completa, aspecto, filtro, scanlines;
  - Audio: volumen, vibración;
  - Juego: ralentización, autodisparo J1/J2 y velocidad, pantalla de inicio;
  - Controles: jugador 1 y 2.
- **Extras:** modo debug, menú de servicio del BIOS, reiniciar la máquina.
- Debajo del panel, una línea de ayuda explica la opción elegida.

**Menú de sonido**, ahora con la misma fuente:

- **Pestaña "Recientes":** los últimos 16 sonidos que pidió el juego. Sirve para identificar uno:
  usar el arma y abrir el menú.
- **F2 o R:** renombrar el sonido elegido (con el teclado).
- **V, o X en el mando:** su vibración. Ciclo: auto → no → leve → media → fuerte. "Auto" es la de
  la tabla de efectos identificados.
- Se guarda en `%APPDATA%\ProjectoDecompile\mslugx\sonidos.ini`. La clave es modo y comando en
  hexadecimal, por ejemplo `10F7=Pistola`.
- Cada fila muestra la clave del driver (por ejemplo `10:F7`), así los nombres que encuentre el
  usuario se pueden pasar al perfil del repo.

**Logo de la pantalla de inicio** (`src/port/logo.c`): el del propio juego.

- En la pantalla de título del attract (cuadro ~1150 con la SRAM vacía), el logo son los sprites
  41-124 más el 380 ("TM"); el fondo rojo son los 21-40.
- La primera vez, el port guarda el estado, corre la máquina en silencio hasta que aparece el
  "TM" (primer tile `0x309CA`), redibuja solo esos sprites (`ng_video_capture_sprites`) y
  restaura el estado. Tarda menos de un segundo.
- El logo queda en `logo_titulo.png` de la carpeta del usuario, nunca en el repo.

**Escala de la interfaz:** continua, como un lienzo de 480 x 300 ajustado a la ventana
(`font_factor`), con `SDL_SCALEMODE_PIXELART` para que los píxeles se vean parejos con escalas
no enteras. Así los menús entran en cualquier tamaño de ventana.

**Corregido:** renombrar dos veces el mismo sonido colgaba el port. `SDL_strdup` reserva con el
asignador de SDL3.dll y el nombre se liberaba con `free()` de otro runtime de C. Ahora el port
hace sus propias copias. Además, la "R" que abre la edición ya no se cuela en el nombre.

**Pruebas:**

- `PD_PRUEBA_VENTANA=N,png[,menu|sonidos|jugar|renombrar]` para capturas de cada pantalla;
  `renombrar` cambia dos veces el nombre de un efecto;
- `ctest` 6/6.
