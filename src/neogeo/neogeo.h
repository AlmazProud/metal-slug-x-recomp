/* Runtime de hardware de la Neo Geo MVS para el port de Metal Slug X.
 *
 * Modela lo que rodea al 68000 recompilado: mapa de memoria, protección del cartucho, LSPC
 * (VRAM, sprites, layer fix, paletas, temporización de líneas e interrupciones), inputs, reloj
 * uPD4990A, watchdog y el latch de sonido. Toda la temporización se expresa en cycles del 68000
 * (12 MHz): 768 cycles por línea, 264 líneas por cuadro.
 *
 * Referencia del hardware: MAME 0.289 (src/mame/snk/neogeo*.cpp, src/devices/machine/
 * upd1990a.cpp), usado SOLO como documentación. Los valores de los puertos se fijaron además
 * contra el oráculo con tools/mame/trace_ports.lua.
 */
#ifndef NEOGEO_H
#define NEOGEO_H

#include <stdint.h>
#include <stdio.h>

#include "cpu68k.h"

#define NG_WIDTH 320
#define NG_HEIGHT 224
#define NG_CYCLES_PER_LINE 768
#define NG_LINES 264
#define NG_CYCLES_PER_FRAME (NG_CYCLES_PER_LINE * NG_LINES)
#define NG_FIRST_VISIBLE_LINE 16          /* vpos 16..239 */
#define NG_VBLANK_LINE 240

/* Entradas (active en alto aquí; el bus las invierte). */
enum {
    NG_UP = 1 << 0, NG_DOWN = 1 << 1, NG_LEFT = 1 << 2, NG_RIGHT = 1 << 3,
    NG_A = 1 << 4, NG_B = 1 << 5, NG_C = 1 << 6, NG_D = 1 << 7,
};
enum { NG_START = 1 << 0, NG_SELECT = 1 << 1, NG_COIN = 1 << 2 };

/* ------------------------------------------------------------------ ROMs */
typedef struct {
    uint8_t *p;              /* P1+P2 en vista del 68000 (big-endian), 5 MB */
    uint32_t p_size;
    uint8_t *bios;           /* sp-u2 en vista del 68000, 128 KB */
    uint8_t *fix_game;      /* formato DotEmu: 4 bpp por filas, 32 bytes por tile */
    uint8_t *fix_bios;
    uint8_t *tiles;          /* formato DotEmu: 4 bpp por filas, 128 bytes por tile */
    uint32_t n_tiles;
    uint8_t *zoom;           /* tabla L0 de reducción vertical (64 KB útiles) */
    uint8_t *z80;            /* M1: programa de sonido, 128 KB */
    uint8_t *adpcm;          /* V1-V3: muestras ADPCM, 10 MB */
    uint32_t adpcm_size;
} NgRoms;

/* Carga y valida (SHA-256) los archivos de la copia del usuario. Devuelve 0 si todo está bien;
 * si no, escribe el motivo en `error`. */
int ng_roms_load(NgRoms *r, const char *carpeta, char *error, int error_size);
void ng_roms_free(NgRoms *r);

/* ------------------------------------------------------------------ máquina */
typedef struct {
    /* memoria */
    uint8_t ram[0x10000];
    uint8_t sram[0x10000];
    uint16_t palette[0x2000];             /* dos bancos de 4096 colores */
    uint32_t pen[2][0x2000];             /* [shadow][índice] -> ARGB8888 */
    uint16_t vram[0x8800];
    /* latch del system (74HC259) */
    uint8_t shadow, cart_vectors, cart_fix, sram_unlocked, palette_bank;
    /* cartucho */
    int p2_bank;
    uint16_t prot_cmd, prot_cnt;
    /* LSPC */
    uint16_t vram_addr, vram_buffer, vram_mod;
    uint8_t anim_speed, anim_disabled, anim_counter, anim_frames;
    uint16_t irq2_control;
    uint32_t irq2_counter;
    int64_t irq2_when;                 /* -1 = sin programar */
    uint8_t irq_vblank, irq_raster, irq3;
    /* E/S */
    uint8_t joy[2], system[2], dsw;     /* system: NG_START | NG_SELECT | NG_COIN */
    uint8_t service, test;
    /* Entradas crudas (bytes tal como los ve el bus, activos en bajo) para reproducir grabaciones
       de MAME: si raw != 0 reemplazan a joy/system/test. */
    uint8_t raw, raw_p1, raw_p2, raw_system, raw_coins, raw_test;
    int64_t watchdog;                    /* ciclo en que vence */
    uint32_t watchdog_resets;
    /* reloj del cuadro */
    int64_t frame_start;               /* ciclo de vpos 0 del cuadro actual */
    int line;                           /* próxima línea cuyo comienzo hay que procesar */
    uint64_t frames;
    /* output */
    uint32_t screen[NG_HEIGHT][NG_WIDTH];
    /* diagnóstico */
    uint64_t unmapped_reads, unmapped_writes;
} NgMachine;

extern NgMachine ng;
extern FILE *ng_trace;                 /* traza de diagnóstico opcional (NULL = no) */
extern FILE *ng_trace_io;              /* escrituras de E/S entre dos cycles (comparación con MAME) */
extern int64_t ng_trace_from, ng_trace_to;
extern FILE *ng_trace_reads;          /* lecturas de los puertos de entrada (diagnóstico) */
/* Gancho opcional: se llama cuando el 68000 reconoce la VBlank (3C000C, bit 2). */
extern void (*ng_on_vblank_ack)(void);
/* Gancho opcional: se llama antes de cada lectura de un puerto de entrada (reproducciones). */
extern void (*ng_before_input_read)(void);
extern NgRoms ng_roms;
extern uint32_t ng_pc_vblank;          /* PC interrumpido por la última VBlank (diagnóstico) */
extern Cpu68k ng_cpu;

/* Fecha inicial del reloj (si no se llama, se usa la hour local). */
void ng_rtc_set(int year, int month, int day, int weekday, int hour, int minute, int second);

void ng_power_on(void);                  /* estado de encendido: reset del 68000 */
void ng_run_frame(void);
/* Próximo límite de tramo (evento de hardware), en cycles del 68000. */
int64_t ng_next_limit(void);
/* Procesa los eventos de hardware vencidos hasta t (sin entregar interrupciones), para ordenar
 * una escritura de video a mitad de instrucción respecto del dibujo de las líneas. */
void ng_catch_up(int64_t t);           /* hasta el comienzo del cuadro siguiente */

/* ------------------------------------------------------------------ mode debug (Fase 6A) */
/* Mediciones que no forman parte del estado de la máquina (no cambian la emulación). */
typedef struct { int16_t x0, x1, y0, y1; } NgBox;      /* x1 < x0: sprite no dibujado */
typedef struct {
    int active;                          /* 1: medir carga y cajas de sprites */
    uint32_t wait_start, wait_end;     /* PCs del bucle de espera de la VBlank del juego */
    uint8_t hide;                     /* NG_OCULTAR_*: capas que no se dibujan */
    int wait_lines;                   /* del último cuadro: inicios de línea con el 68000 esperando */
    int wait_lines_current;
    NgBox box[381];                    /* del último cuadro, en píxeles visibles (0-319, 0-223) */
    NgBox box_current[381];
} NgDebug;
enum { NG_HIDE_SPRITES = 1, NG_HIDE_FIX = 2 };
extern NgDebug ng_dbg;

/* ------------------------------------------------------------------ lectura forzada (Fase 7A) */
/* Palabras de la RAM de trabajo que el código del cartucho (PC < 0xC00000) lee con otro value:
 * (real & ~mask) | value. El BIOS ve la RAM real (su test de RAM y su menú de service no se
 * enteran). Sirve para encender opciones del juego sin tocar la SRAM: una slot por uso. */
typedef struct { int active; uint32_t dir; uint16_t mask, value; } NgForcedRead;
enum { NG_FORCED_BLOOD, NG_FORCED_DIPS, NG_FORCED_DEVMODE, NG_FORCED_COUNT };
extern NgForcedRead ng_forced_read[NG_FORCED_COUNT];

/* ------------------------------------------------------------------ estados guardados */
/* State completo de la máquina, tomado entre frames (después de ng_run_frame): 68000,
 * hardware de video y E/S, reloj, Z80 y YM2610. ng_state_save devuelve un búfer de malloc;
 * ng_state_load devuelve 0 si el estado es válido para esta versión del port (si no, no toca
 * la máquina). */
uint8_t *ng_state_save(size_t *tam);
int ng_state_load(const uint8_t *data, size_t tam);

typedef struct {
    uint8_t *data;
    size_t tam, pos;
    int saving;
} NgState;
void ng_state_block(NgState *e, void *p, size_t n);   /* guarda o restaura n bytes */
void ng_machine_state(NgState *e);
void ng_sound_state(NgState *e);
void ng_rtc_state(NgState *e);

/* ------------------------------------------------------------------ internos */
void ng_video_init(void);
void ng_video_line_start(int line);   /* dibuja la anterior y prepara la lista de sprites */
/* Solo los sprites [desde, hasta], sobre fondo transparente (0): imagen de NG_WIDTH x NG_HEIGHT. */
void ng_video_capture_sprites(uint32_t *destino, int desde, int hasta);
void ng_video_anim_tick(void);
uint16_t ng_video_read(uint32_t reg);
void ng_video_write(uint32_t reg, uint16_t v);
void ng_palette_write(uint32_t indice, uint16_t v);
void ng_irq_update(void);

void ng_rtc_init(void);
void ng_rtc_write(uint8_t dato);      /* bits: 0 DATA IN, 1 CLK, 2 STB */
int ng_rtc_tp(void);
int ng_rtc_data(void);

void ng_sound_init(void);
void ng_sound_until(int64_t cycles_68k);          /* avanza Z80 y YM2610 hasta ese ciclo */
int64_t ng_sound_next_event(void);            /* próximo temporizador del YM (ciclo del 68000) */
void ng_sound_command(uint8_t v);
uint8_t ng_sound_reply(void);

/* Fase 6B: gancho opcional, llamado con cada command de sonido del 68000 (vibración). */
extern void (*ng_on_sound_command)(uint8_t command);
/* Notas iniciadas por tipo de canal del YM2610 desde el encendido (clasificar sonidos; no son
 * parte del estado de la máquina). */
typedef struct { uint32_t fm, ssg, adpcm_a, adpcm_b; } NgNotes;
extern NgNotes ng_notes;
/* Solo sonido (menú de música): el 68000 queda stopped y el Z80 y el YM2610 avanzan solos. La
 * máquina queda en un estado que no es el del juego: guardarlo antes y restaurarlo después. */
void ng_sound_only_command(uint8_t command);
void ng_sound_only_advance(int64_t cycles_68k);

/* Audio generado (estéreo, 16 bits, 55 555 Hz). Solo se guarda si ng_audio_enabled != 0. */
extern int ng_audio_enabled;
#define NG_AUDIO_HZ 55555
uint32_t ng_audio_read(int16_t *destino, uint32_t max_muestras);

void ng_bus_init(void);

#endif
