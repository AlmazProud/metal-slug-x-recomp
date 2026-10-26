/* C bridge to ymfm (C++, BSD-3) for the Neo Geo YM2610.
 *
 * ym2610.cpp wraps ymfm::ym2610 with the same configuration MAME uses (medium fidelity:
 * one sample every 144 YM cycles, 8 MHz / 144 = 55 555 Hz). Timers, the IRQ line, the "busy"
 * state and the ADPCM ROM reads are handled by the runtime (sound.c), which implements the
 * ng_ym_* functions.
 */
#ifndef YM2610_H
#define YM2610_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void ym_reset(void);
void ym_ssg_clock(uint32_t count);                 /* SSG engine only, no output (PORT CHANGE) */
uint8_t ym_read(uint32_t offset);                 /* offset 0-3 (Z80 ports 4-7) */
void ym_write(uint32_t offset, uint8_t data);
void ym_generate(int32_t output[3]);               /* one sample: FM left, FM right, SSG */
void ym_timer_expired(uint32_t n);
/* Chip state (save states). */
uint32_t ym_state_size(void);
void ym_state_save(uint8_t *dest);
void ym_state_load(const uint8_t *data, uint32_t n);

/* Implemented by the runtime. */
void ng_ym_timer(uint32_t n, int32_t ym_cycles);   /* < 0: cancel */
void ng_ym_busy_until(uint32_t ym_cycles);
int ng_ym_busy(void);
void ng_ym_irq(int active);
uint8_t ng_ym_rom(uint32_t address);

#ifdef __cplusplus
}
#endif

#endif
