# Fase 6: "Remaster"

Mejoras opcionales sobre el port de la Fase 5. Todas se apagan desde el menú, y ninguna cambia la
emulación cuando está apagada: `ctest` (con la Misión 1 contra MAME) sigue pasando.

| Subfase | Contenido | Estado |
|---|---|---|
| 6A | Disparo automático y modo debug | Cerrada (2026-10-12) |
| 6B | Catálogo de sonidos, menú de música, voces y efectos; vibración del mando | Pendiente |
| 6C | Menú de inicio moderno con la fuente del juego | Pendiente |
| 6D | Pantalla ancha: investigación primero | Pendiente |

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
- Las direcciones propias de Metal Slug X están en `src/port/perfil.h`, primer paso hacia un
  perfil de juego.

**Pruebas:**

- `PD_DEBUG_VISTAS=cajas,paleta,memoria` abre las vistas al activar el modo.
- Con `PD_PRUEBA_VENTANA`, la ventana corre sin esperar al reloj.
