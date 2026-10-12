/* Latch de sonido 68000 <-> Z80: SUSTITUTO TEMPORAL de la Fase 3A (sin Z80 ni YM2610).
 *
 * El BIOS y el juego esperan respuestas del programa del Z80 (comando 1 y 3 en el arranque;
 * ver BIOS C11F88/C11FE8: si no llega la respuesta 1, el BIOS se detiene con el error 8).
 * Comportamiento medido en MAME con herramientas/mame/traza_sonido.lua (60 s de arranque y
 * attract mode, nvram vacía):
 *   - comando 0x01 -> la respuesta pasa a 0x01;
 *   - comando 0x03 (reinicio del programa de sonido) -> la respuesta pasa a 0x00;
 *   - el resto de los comandos (música y efectos) no cambian la respuesta.
 * Latencia medida con herramientas/mame/latencia_z80.lua (tiempo de máquina entre la escritura
 * del 68000 y la escritura del Z80 en su puerto 0x0C), muy estable entre repeticiones:
 *   - comando 0x01: 14 660,5-14 662 us  -> 175 929 ciclos del 68000;
 *   - comando 0x03: 60,4-61,5 us         -> 729 ciclos.
 * En la Fase 3B se reemplaza por el Z80 real ejecutando el M1 del cartucho.
 */
#include "neogeo.h"

#define RETARDO_CMD1 175929
#define RETARDO_CMD3 729

void ng_sonido_comando(uint8_t v)
{
    ng.snd_comando = v;
    if (ng_traza)
        fprintf(ng_traza, "%.3f f=%llu linea=%lld pc=%06X CMD %02X\n", ng_cpu.ciclos / 12.0,
                (unsigned long long)ng.cuadros, (long long)((ng_cpu.ciclos - ng.inicio_cuadro) / NG_CICLOS_LINEA), ng_cpu.pc, v);
    if (v == 0x01 || v == 0x03) {
        ng.snd_respuesta_pendiente = 1 + (v == 0x01);      /* 1 + respuesta que llegará */
        ng.snd_cuando = ng_cpu.ciclos + (v == 0x01 ? RETARDO_CMD1 : RETARDO_CMD3);
    }
}

uint8_t ng_sonido_respuesta(void)
{
    if (ng.snd_respuesta_pendiente && ng_cpu.ciclos >= ng.snd_cuando) {
        ng.snd_respuesta = (uint8_t)(ng.snd_respuesta_pendiente - 1);
        ng.snd_respuesta_pendiente = 0;
    }
    return ng.snd_respuesta;
}
