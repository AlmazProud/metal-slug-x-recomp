# Fase 7A: V-sync y sangre roja

Las dos son opciones del menú y no cambian la emulación: `ctest` (con la Misión 1 contra MAME)
sigue pasando.

## Sangre roja

**Hallazgo:** la sangre blanca no depende de la región, sino de una opción del propio juego. La
cabecera del P ROM declara las opciones que el BIOS muestra en el menú de servicio ("soft dips").
En la tabla de EE. UU. y Europa están:

- HERO (vidas) y CONTINUE;
- DIFFICULTY (LEVEL 1-8) y PLAY TIME (60-90);
- DEMO SOUND, PLAY MANUAL y **BLOOD (OFF/ON)**;
- CREDIT/LEVEL.

En EE. UU., BLOOD viene en OFF.

Notas:

- El archivo de GOG guarda las palabras con los bytes invertidos ("EN-OEG"). `roms.c` ya los
  devuelve a la vista del 68000.
- El BIOS copia las opciones a `0x10FD84`: dos tiempos, dos cuentas y diez opciones desde
  `0x10FD8A`, así que BLOOD queda en `0x10FD8F`.
- El juego la consulta en `0x1B2BE` (`tst.b $10FD8F`).
- También se descartaron otras pistas:
  - `0x100003` (decidido en `0x2382`) separa los créditos por jugador, como en EE. UU.;
  - `0x100000` y `0x100001` son el idioma y el país.

**Opción "Sangre: Original / Roja"** (Configuración → Juego, o `sangre=1` en el ini):

- Escribir ese byte desde el encendido rompía el test de RAM del BIOS ("WORK RAM ERROR
  10FD8E").
- Por eso se fuerza **solo la lectura del código del juego** (`ng_lectura_forzada` en el bus:
  PC < `0xC00000`).
- El BIOS ve la RAM real y su menú de servicio sigue mostrando el ajuste del usuario.
- Tampoco toca la SRAM. Activar BLOOD en el menú de servicio sigue funcionando igual.

**Comprobación:** con la grabación de la Misión 1, la primera diferencia de imagen está en el
cuadro 1459, el primer enemigo que muere. Las capturas muestran la salpicadura blanca del original
y la roja con la opción. `--sangre` en el modo sin ventana permite repetirlo.

## V-sync

Opción en Configuración → Video (`vsync=1` por defecto). Hay tres ritmos posibles:

| Situación | Ritmo | Audio |
|---|---|---|
| Sin V-sync | Un cuadro cada 16,896 ms (59,1856 Hz, la placa) | Como antes |
| V-sync con monitor de 58 a 62 Hz | Un cuadro por refresco: el juego va a la frecuencia del monitor (1,4 % más rápido a 60 Hz), como el `-syncrefresh` de MAME | Se reproduce en la misma proporción (`SDL_SetAudioStreamFrequencyRatio`), con una corrección de hasta ±0,5 % según la cola, para mantenerla en ~50 ms |
| V-sync con otras frecuencias (75, 120, 144 Hz...) | Se presenta en cada refresco y un acumulador decide cuántos cuadros emular al ritmo exacto de la placa | Ídem |

**Medición** (20 s reales, `PD_SALIR_TRAS` + `--traza`, monitor de 60 Hz):

| Modo | Cuadros emulados/s | Cola de audio media | Cola vacía | Descartes |
|---|---|---|---|---|
| V-sync (60 Hz) | 59,966 | 40,6 ms | 0 | 0 |
| Sin V-sync | 59,184 | 39,3 ms | 0 | 0 |
| V-sync, 144 Hz simulados (`PD_HZ=144`) | 59,098 | 41,5 ms | 0 | 0 |
