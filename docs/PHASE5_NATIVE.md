# Fase 5: mejoras nativas

El port se juega como un programa de Windows normal: busca solo la instalación de GOG, tiene un
menú propio, estados guardados, filtros de imagen, controles configurables para 2 jugadores y una
opción para quitar la ralentización original. Todo lo que cambia el comportamiento del juego es
opcional y está apagado por defecto: el modo normal sigue siendo idéntico a MAME (Fase 4).

## Uso

`mslugx.exe` sin opciones. Las ROMs se buscan en este orden:

1. `--roms CARPETA`;
2. la carpeta recordada en `mslugx.ini`;
3. `roms\` junto al ejecutable;
4. la instalación de GOG: registro de Windows `HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\2046360890`
   (o cualquier juego registrado como "Metal Slug X") y carpetas habituales en cada unidad;
5. si nada sirve, un selector de carpeta (vale la raíz del juego o `resources\game`).

Los archivos se validan siempre por SHA-256 (`src/neogeo/roms.c`).

`cmake --install build --prefix CARPETA` deja `mslugx.exe` y `SDL3.dll` listos para copiar. El
ejecutable contiene código traducido de la copia del usuario: es para uso personal, no para
redistribuir.

### Teclas fijas

| Tecla | Acción |
|---|---|
| Esc (mando: Guide, o Back + Start) | menú |
| F5 / F9 | guardar / cargar estado en la ranura activa |
| F11 o Alt+Enter | pantalla completa |
| F2 | botón de test: menú de servicio del BIOS (dificultad, vidas, etc.) |
| F12 | captura BMP en la carpeta de preferencias |
| P | pausa |

Controles por defecto:

- **Jugador 1:**
  - flechas;
  - Z / X / C / V = A (disparo) / B (salto) / C (granada) / D;
  - 1 = Start, 5 = moneda.
- **Jugador 2:**
  - I / K / J / L para moverse;
  - A / S / D / F = A / B / C / D;
  - 2 = Start, 6 = moneda.
- **Mandos:** el primero que se conecta es el jugador 1 y el segundo, el jugador 2. Con la
  disposición de Xbox: X / A / B / Y = A / B / C / D; cruceta o palanca izquierda; Start; Back =
  moneda.

### Menú

- Continuar.
- Guardar o cargar estado, en 9 ranuras (← / → cambia la ranura).
- Ralentización: "sí (original)", "no (68000 x2)" o "no (68000 x4)".
- Pantalla completa.
- Aspecto: 4:3, como el monitor, o píxeles cuadrados.
- Filtro: nítido, suave o píxeles; scanlines.
- Volumen.
- Controles de cada jugador, con captura de teclas y botones.
- Menú de servicio, reinicio y salir.

Se navega con el teclado o el mando.

### Archivos del usuario

Todos viven en `%APPDATA%\ProjectoDecompile\mslugx\`:

- `mslugx.ini`: configuración, formato descrito en `src/port/config.c`;
- `sram.bin`;
- `estado_N.bin`;
- capturas.

## Imagen

La Neo Geo dibuja 320×224 y el monitor lo estira a 4:3, así que los píxeles no son cuadrados
(ancho/alto ≈ 0,93). Modos:

- **Aspecto 4:3** (por defecto): la imagen ocupa 4:3 dentro de la ventana, con bandas negras si
  sobra espacio.
- **Píxeles cuadrados:** 320:224. Si además el filtro es "píxeles", usa una escala entera exacta.
- **Filtro nítido** (por defecto): primero amplía por un factor entero sin suavizar (el menor que
  cubre el tamaño final) y después ajusta con interpolación lineal. Todos los píxeles quedan del
  mismo tamaño y los bordes no se ven borrosos.
- **Scanlines:** una franja oscura en la mitad inferior de cada línea original. Solo se dibuja
  desde 2 píxeles de pantalla por línea.

## Ralentización original y turbo

**Medición.** El juego corre su lógica a 30 Hz: el bucle principal espera a la VBlank (PC
`0x39B0`-`0x3A3E`) y sale en `0x3A42` una vez cada 2 cuadros. Cuando la lógica de un ciclo no
termina a tiempo, el ciclo dura 3 o 4 cuadros (20 o 15 Hz): esa es la ralentización que se ve con
muchas explosiones. Se mide con `PD_CONTAR_PC=3A42` (sin ventana).

**Turbo** (`src/cpu/cycles68k.h`, macro `CIC`): cada instrucción cuesta 2^k veces menos ciclos.
Es un 68000 más rápido frente al mismo hardware de video y sonido, como el overclock de MAME.
Con k = 0 no cambia nada: las 5 pruebas, incluida la de la Misión 1 contra MAME, pasan igual.

Cuadros perdidos por la ralentización (intervalos de más de 2 cuadros entre vueltas del bucle,
en tramos de juego):

| Corrida | Original | Turbo x2 | Turbo x4 |
|---|---|---|---|
| Demo del attract mode (12 000 cuadros) | 10,3 % | 0,2 % | 0,0 % |
| Misión 1 (grabación del usuario) | 9,0 % | 0,0 % | — |

Notas sobre la medición:

- El demo del attract es el mismo con cualquier turbo.
- La grabación de la Misión 1 con turbo deja de coincidir con la partida original, porque el
  juego avanza más rápido que las entradas grabadas, pero sirve como carga de juego.
- Turbo x2 basta. En el ini existe x4 por si algún tramo lo pide.

## Estados guardados

`ng_state_save` y `ng_state_load` (`src/neogeo/savestate.c`) guardan la máquina completa
entre cuadros, en unos 835 KB:

- 68000 y hardware de video y E/S (`NgMachine`, que incluye la SRAM);
- planificador;
- uPD4990A;
- Z80 y estado del sonido;
- YM2610, con el `save_restore` de ymfm.

El código recompilado no guarda estado propio entre cuadros: al terminar cada cuadro, todas las
llamadas nativas se deshacen.

Un estado lleva versión y tamaño. Si no coinciden con el ejecutable, se rechaza sin tocar la
máquina.

**Test:** `ctest` → `estado_guardado` (`--probar-estado 600`). Guarda un estado, corre 600
cuadros, reinicia la máquina, carga el estado y repite. RAM, imagen y audio salen idénticos. Lo
mismo se comprobó en la Misión 1 (900 cuadros desde el cuadro 5000).

## Herramientas de prueba nuevas

- `PD_PRUEBA_VENTANA=N,archivo.png[,menu]`: tras N cuadros guarda lo que muestra la ventana (con
  el menú abierto si se pide) y sale.
- `PD_PREF=carpeta\`: otra carpeta de preferencias, para no tocar la configuración del usuario.
- `PD_CONTAR_PC=hex` y `PD_CONTAR_ARCHIVO`: vueltas por cuadro de una dirección.
- Las huellas V (`--huellas`) incluyen el PC interrumpido por la VBlank (`int=`).
