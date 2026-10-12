/* Puente en C hacia ymfm (C++, BSD-3) para el YM2610 de la Neo Geo.
 *
 * ym2610.cpp envuelve ymfm::ym2610 con la misma configuración que usa MAME (fidelidad media:
 * una muestra cada 144 cycles del YM, 8 MHz / 144 = 55 555 Hz). Los temporizadores, la línea de
 * IRQ, el estado "ocupado" y las lecturas de la ROM ADPCM los resuelve el runtime (sound.c)
 * implementando las funciones ng_ym_*.
 */
#ifndef YM2610_H
#define YM2610_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ym_reset(void);
uint8_t ym_read(uint32_t offset);                 /* offset 0-3 (puertos 4-7 del Z80) */
void ym_write(uint32_t offset, uint8_t dato);
void ym_generate(int32_t output[3]);               /* una muestra: FM izq., FM der., SSG */
void ym_timer_expired(uint32_t n);
/* State del chip (estados guardados). */
uint32_t ym_state_size(void);
void ym_state_save(uint8_t *destino);
void ym_state_load(const uint8_t *data, uint32_t n);

/* Las implementa el runtime. */
void ng_ym_timer(uint32_t n, int32_t ciclos_ym);   /* < 0: cancelar */
void ng_ym_busy_until(uint32_t ciclos_ym);
int ng_ym_busy(void);
void ng_ym_irq(int active);
uint8_t ng_ym_rom(uint32_t direccion);

#ifdef __cplusplus
}
#endif

#endif
