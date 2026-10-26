/* Neo Geo MVS hardware runtime, shared by the Metal Slug ports (the game comes from ng_game).
 *
 * Models what surrounds the recompiled 68000: memory map, cartridge protection, LSPC
 * (VRAM, sprites, fix layer, palettes, line timing and interrupts), inputs, uPD4990A clock,
 * watchdog and the sound latch. All timing is expressed in 68000 cycles (12 MHz):
 * 768 cycles per line, 264 lines per frame.
 *
 * Hardware reference: MAME 0.289 (src/mame/snk/neogeo*.cpp, src/devices/machine/
 * upd1990a.cpp), used ONLY as documentation. The port values were also pinned down against
 * the oracle with tools/mame/trace_ports.lua.
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

/* Inputs (active high here; the bus inverts them). */
enum {
    NG_UP = 1 << 0, NG_DOWN = 1 << 1, NG_LEFT = 1 << 2, NG_RIGHT = 1 << 3,
    NG_A = 1 << 4, NG_B = 1 << 5, NG_C = 1 << 6, NG_D = 1 << 7,
};
enum { NG_START = 1 << 0, NG_SELECT = 1 << 1, NG_COIN = 1 << 2 };

/* ------------------------------------------------------------------ game descriptor */
/* Everything that changes from one game to another, for the runtime and the frontend. Each
 * executable links the descriptor of its game (src/games/<id>.c), which defines `ng_game`.
 * Descriptors hold hashes and addresses only, never game data. */
/* NG_CART_PVC: the NEO-PVC chip (Metal Slug 5): 8 KB of RAM over the top of the P2 window, whose
   bytes 2FFFF0-2FFFF3 select the P2 window (bus.c, docs/PHASE2_HARDWARE.md). */
/* NG_CART_STANDARD: no protection chip, the P2 bank selected by a write to 2FFFF0-2FFFFF (Metal
   Slug 4). */
enum { NG_CART_ALTERA_MSLUGX = 1, NG_CART_SMA = 2, NG_CART_PVC = 3, NG_CART_STANDARD = 4 };
/* Fix layer banking of cartridges with more than 128 KB of fix tiles (CMC chips). ROWS: one
 * 128 KB bank per tile row, taken from a table the game writes at VRAM 0x7500/0x7580 (the
 * "Garou" type in MAME's documentation; Garou and Metal Slug 3). */
enum { NG_FIX_BANK_NONE = 0, NG_FIX_BANK_ROWS = 1 };

/* DotEmu files (PC releases), one per role. */
enum { NG_DOTEMU_P, NG_DOTEMU_BIOS, NG_DOTEMU_FIX_GAME, NG_DOTEMU_FIX_BIOS, NG_DOTEMU_TILES,
       NG_DOTEMU_ZOOM, NG_DOTEMU_Z80, NG_DOTEMU_ADPCM, NG_DOTEMU_ROLES };
typedef struct {
    const char *name;        /* e.g. "mslugx_game_m68k" */
    uint32_t size;
    const char *sha256;      /* of the reference (GOG) copy */
} NgDotEmuFile;

/* Code Mystics files (Metal Slug 4's GOG release: Data\rom\*.bin), one per role, the same shape:
   MAME's regions after MAME's own transformations, the sprites in packed pixels (Metal Slug 4's
   docs/ROM_MAP.md). */
enum { NG_CM_P, NG_CM_TILES, NG_CM_FIX_GAME, NG_CM_Z80, NG_CM_ADPCM, NG_CM_ROLES };

/* A cartridge file of the MAME set. Files of the same kind are joined in `index` order: P and V
 * concatenated, C in pairs (odd, even) per 16 MB of sprites. X: a chip's own ROM (NEO-SMA). */
enum { NG_MAME_P, NG_MAME_S, NG_MAME_M, NG_MAME_V, NG_MAME_C, NG_MAME_X, NG_MAME_KINDS };

/* Encrypted cartridges (Metal Slug 3: NEO-SMA program, CMC42 sprites with the fix in them): the
 * game's own decryption of its MAME set into the PC release's layout (src/games/<id>/crypt.c). */
typedef struct {
    uint32_t program_size;
    /* p: the P files joined; chip: the X file. out: program_size bytes. */
    void (*program)(const uint8_t *p, uint32_t p_size, const uint8_t *chip, uint8_t *out);
    /* image: the C files interleaved in pairs (odd file: even bytes); region: the decrypted
       sprites in the same layout. */
    void (*sprites)(const uint8_t *image, uint32_t size, uint8_t *region);
    uint32_t fix_size;
    void (*fix)(const uint8_t *region, uint8_t *s);    /* the fix layer, Neo Geo S format */
    /* Optional (Metal Slug 5: CMC50 M1, PCM2 samples), in place: the M files joined (the Z80
       program; 0 = decrypted, -1 = not this game's key) and the V files joined. NULL: plain. */
    int (*z80)(uint8_t *m, uint32_t size);
    void (*adpcm)(uint8_t *v, uint32_t size);
} NgMameCrypt;
typedef struct {
    int kind, index;
    const char *name;        /* MAME's name (for messages; files are matched by hash) */
    uint32_t size;
    uint32_t crc;
    const char *sha1;
    const char *label;       /* P1 (kind P, index 0): "arcade" or "gog", the program version */
} NgMameFile;

typedef struct {
    const char *id;          /* "mslugx": DotEmu prefix, executable, settings folder */
    const char *title;       /* "Metal Slug X" */
    const char *project;     /* "metal-slug-x-recomp" */
    const char *gog_id;      /* GOG Galaxy registry id */
    NgDotEmuFile dotemu[NG_DOTEMU_ROLES];   /* names NULL: none (with no PC release: reference files) */
    NgDotEmuFile codemystics[NG_CM_ROLES];  /* names NULL: none (Metal Slug 4's GOG release) */
    const char *gog_p1_sha256;   /* first MB of the 68000 program (file byte order); NULL: none */
    const NgMameFile *mame;  /* NULL: MAME sets of this game are not supported */
    int n_mame;
    const NgMameCrypt *crypt;   /* NULL: the MAME set's files are plain (unless mame_encrypted) */
    int mame_encrypted;      /* the MAME set needs a decryption the runtime does not have yet:
                                it is recognized and reported, not loaded */
    /* cartridge hardware */
    int cart;                /* NG_CART_* */
    int fix_bank;            /* NG_FIX_BANK_* */
    /* NG_CART_SMA: the NEO-SMA chip, as observed in MAME (docs/ROM_MAP.md). A write to
       sma_bank_reg selects a 1 MB P2 window: the value's bits sma_bank_bits (low first) form
       an index into sma_banks, offsets from the start of P2 (program byte 0x100000). */
    uint32_t sma_bank_reg;
    uint8_t sma_bank_bits[6];
    const uint32_t *sma_banks;           /* 64 entries */
    uint32_t sma_id_addr;                /* reads as sma_id */
    uint16_t sma_id;
} NgGame;

extern const NgGame *const ng_game;

/* A PC release exists (GOG, Steam): the frontend searches its installs and names it. Metal Slug 5
   has none; its DotEmu-layout files are a decrypted reference made from MAME's regions
   (tools/make_reference.py). */
static inline int ng_has_pc_release(void) { return ng_game->gog_id && ng_game->gog_id[0]; }

/* ------------------------------------------------------------------ ROMs */
enum { NG_FORMAT_DOTEMU = 1, NG_FORMAT_MAME = 2, NG_FORMAT_CODEMYSTICS = 3 };
enum { NG_VERSION_UNKNOWN = 0, NG_VERSION_GOG = 1, NG_VERSION_ARCADE = 2 };

/* What was loaded (roms.c): the recompiled program and the game profile depend on it. */
typedef struct {
    char source[1024];       /* folder or zip it was loaded from */
    char description[160];   /* for the user (English) */
    char bios[24];           /* MAME's -bios name ("us", "euro", "unibios40"...) */
    int format;              /* NG_FORMAT_* */
    int version;             /* NG_VERSION_*: the 68000 program (P1) */
    int verified;            /* every file has a known hash */
    uint8_t p1_sha256[32];   /* P1 and BIOS as files (before the byte swap) */
    uint8_t bios_sha256[32];
} NgRomInfo;

typedef struct {
    uint8_t *p;              /* the 68000 program as seen by the 68000 (big-endian): P1, then P2 */
    uint32_t p_size;
    uint8_t *bios;           /* sp-u2 as seen by the 68000, 128 KB */
    uint8_t *fix_game;      /* DotEmu format: 4 bpp in rows, 32 bytes per tile */
    uint32_t fix_game_size;
    uint8_t *fix_bios;
    uint8_t *tiles;          /* DotEmu format: 4 bpp in rows, 128 bytes per tile */
    uint32_t n_tiles;
    uint8_t *zoom;           /* L0 vertical shrink table (64 KB used) */
    uint8_t *z80;            /* M1: sound program */
    uint32_t z80_size;
    uint8_t *adpcm;          /* V ROMs: ADPCM samples */
    uint32_t adpcm_size;
    NgRomInfo info;
} NgRoms;

/* Loads the user's copy of ng_game from `path` (a folder, searched a few levels deep, or a zip):
 * the PC release's DotEmu files or a MAME set (game + neogeo). Returns 0 if a complete copy was
 * found; otherwise writes the reason into `error`. */
int ng_roms_load(NgRoms *r, const char *path, char *error, int error_size);
/* Another folder searched as well (NULL: none): the window's "game" folder next to the program,
 * where a GOG copy's missing Neo Geo system files can go (Metal Slug 4: docs/PHASE5_LOADER.md). */
extern const char *ng_roms_system_path;
void ng_roms_free(NgRoms *r);

/* ------------------------------------------------------------------ machine */
typedef struct {
    /* memory */
    uint8_t ram[0x10000];
    uint8_t sram[0x10000];
    uint16_t palette[0x2000];             /* two banks of 4096 colours */
    uint32_t pen[2][0x2000];             /* [shadow][index] -> ARGB8888 */
    uint16_t vram[0x8800];
    /* system latch (74HC259) */
    uint8_t shadow, cart_vectors, cart_fix, sram_unlocked, palette_bank;
    /* cartridge */
    uint32_t p2_base;                    /* program byte mapped at 0x200000 (P2 window) */
    uint8_t cart_ram[0x2000];            /* NG_CART_PVC: 2FE000-2FFFFF, 68000 byte order */
    uint16_t prot_cmd, prot_cnt;
    /* LSPC */
    uint16_t vram_addr, vram_buffer, vram_mod;
    uint8_t anim_speed, anim_disabled, anim_counter, anim_frames;
    uint16_t irq2_control;
    uint32_t irq2_counter;
    int64_t irq2_when;                 /* -1 = not scheduled */
    uint8_t irq_vblank, irq_raster, irq3;
    /* I/O */
    uint8_t joy[2], system[2], dsw;     /* system: NG_START | NG_SELECT | NG_COIN */
    uint8_t service, test;
    /* Raw inputs (bytes exactly as the bus sees them, active low) to replay MAME recordings:
       if raw != 0 they replace joy/system/test. */
    uint8_t raw, raw_p1, raw_p2, raw_system, raw_coins, raw_test;
    int64_t watchdog;                    /* cycle at which it expires */
    uint32_t watchdog_resets;
    int64_t eclock_origin;               /* cycle of the last 68000 reset: E clock phase */
    int32_t video_offset;                /* line starts (drawing, sprite lists), relative to
                                            frame_start (-1 after a watchdog reset) */
    /* frame clock */
    int64_t frame_start;               /* cycle of vpos 0 of the current frame */
    int line;                           /* next line whose start must be processed */
    uint64_t frames;
    /* output */
    uint32_t screen[NG_HEIGHT][NG_WIDTH];
    /* diagnostics */
    uint64_t unmapped_reads, unmapped_writes;
} NgMachine;

extern NgMachine ng;
extern FILE *ng_trace;                 /* optional diagnostic trace (NULL = off) */
extern FILE *ng_trace_io;              /* I/O writes between two cycles (comparison with MAME) */
extern int64_t ng_trace_from, ng_trace_to;
extern uint32_t ng_watch_write;          /* work RAM word whose writes are logged to stdout (0 = off) */
/* Optional hook on one work RAM word (ng_hook_word, even address; 0 = off): called after each
   write to it with the word before and after (the frontend's Boss Rush, bossrush.c). */
extern uint32_t ng_hook_word;
extern void (*ng_on_hook_write)(uint16_t before, uint16_t after);
extern FILE *ng_trace_reads;          /* input port reads (diagnostics) */
/* Optional hook: called when the 68000 acknowledges the VBlank (3C000C, bit 2). */
extern void (*ng_on_vblank_ack)(void);
/* Optional hook: called before each input port read (replays). */
extern void (*ng_before_input_read)(void);
extern NgRoms ng_roms;
extern uint32_t ng_pc_vblank;          /* PC interrupted by the last VBlank (diagnostics) */
extern Cpu68k ng_cpu;

/* Initial clock date (if not called, the local time is used). */
void ng_rtc_set(int year, int month, int day, int weekday, int hour, int minute, int second);

void ng_power_on(void);
/* Between frames: calls a game routine and restores the 68000 afterwards (see machine.c). */
int ng_call68k(uint32_t target, const uint32_t d[8], const uint32_t a[8], long max_steps);                  /* power-on state: 68000 reset */
void ng_run_frame(void);
/* Next slice limit (hardware event), in 68000 cycles. */
int64_t ng_next_limit(void);
/* Processes the hardware events due up to t (without delivering interrupts), to order a
 * mid-instruction video write relative to the drawing of the lines. */
void ng_catch_up(int64_t t);           /* up to the start of the next frame */

/* ------------------------------------------------------------------ debug mode (Phase 6A) */
/* Measurements that are not part of the machine state (they do not change the emulation). */
typedef struct { int16_t x0, x1, y0, y1; } NgBox;      /* x1 < x0: sprite not drawn */
typedef struct {
    int active;                          /* 1: measure load and sprite boxes */
    uint32_t wait_start, wait_end;     /* PCs of the game's VBlank wait loop */
    uint8_t hide;                     /* NG_HIDE_*: layers that are not drawn */
    int wait_lines;                   /* from the last frame: line starts with the 68000 waiting */
    int wait_lines_current;
    NgBox box[381];                    /* from the last frame, in visible pixels (0-319, 0-223) */
    NgBox box_current[381];
} NgDebug;
enum { NG_HIDE_SPRITES = 1, NG_HIDE_FIX = 2 };
extern NgDebug ng_dbg;

/* ------------------------------------------------------------------ forced reads (Phase 7A) */
/* Work RAM words that the cartridge code (PC < 0xC00000) reads with a different value:
 * (real & ~mask) | value. The BIOS sees the real RAM (its RAM test and service menu are not
 * affected). Used to turn on game options without touching the SRAM: one slot per use. */
/* pc_first/pc_last != 0: only for reads made while the 68000 is in that range (in recompiled
 * code the PC is the entry of the running function, so the range must cover it). */
typedef struct { int active; uint32_t addr; uint16_t mask, value; uint32_t pc_first, pc_last; } NgForcedRead;
enum { NG_FORCED_BLOOD, NG_FORCED_DIPS, NG_FORCED_DEVMODE, NG_FORCED_DEVMODE_COPY, NG_FORCED_FAT_STAGE,
       NG_FORCED_MANUAL, NG_FORCED_COUNT };
extern NgForcedRead ng_forced_read[NG_FORCED_COUNT];

/* ------------------------------------------------------------------ save states */
/* Complete machine state, taken between frames (after ng_run_frame): 68000, video and I/O
 * hardware, clock, Z80 and YM2610. ng_state_save returns a malloc'd buffer; ng_state_load
 * returns 0 if the state is valid for this version of the port (otherwise it leaves the
 * machine untouched). */
uint8_t *ng_state_save(size_t *size);
int ng_state_load(const uint8_t *data, size_t size);

typedef struct {
    uint8_t *data;
    size_t size, pos;
    int saving;
} NgState;
void ng_state_block(NgState *e, void *p, size_t n);   /* saves or restores n bytes */
void ng_machine_state(NgState *e);
void ng_sound_state(NgState *e);
void ng_rtc_state(NgState *e);

/* ------------------------------------------------------------------ internals */
void ng_video_init(void);
void ng_video_line_start(int line);   /* draws the previous line and prepares the sprite list */
/* Only sprites [first, last], over a transparent background (0): NG_WIDTH x NG_HEIGHT image. */
void ng_video_capture_sprites(uint32_t *dest, int first, int last);
void ng_video_anim_tick(void);
uint16_t ng_video_read(uint32_t reg);
void ng_video_write(uint32_t reg, uint16_t v);
void ng_palette_write(uint32_t index, uint16_t v);
void ng_irq_update(void);

void ng_rtc_init(void);
void ng_rtc_write(uint8_t data);      /* bits: 0 DATA IN, 1 CLK, 2 STB */
int ng_rtc_tp(void);
int ng_rtc_data(void);

void ng_sound_init(void);
/* Watchdog reset of the sound side (sound.c: what restarts and what goes on). */
void ng_sound_reset(void);
void ng_sound_until(int64_t cycles_68k);          /* advances the Z80 and YM2610 up to that cycle */
int64_t ng_sound_next_event(void);            /* next YM timer (68000 cycle) */
/* Command from the 68000, written at 68000 cycle `when` (one cycle into the write access, where
   MAME runs the write handler: the Z80 sees it from there, as the latch write synchronises both
   CPUs in MAME). */
void ng_sound_command(uint8_t v, int64_t when);
uint8_t ng_sound_reply(void);

/* Phase 6B: optional hook, called with every sound command from the 68000 (rumble). */
extern void (*ng_on_sound_command)(uint8_t command);
/* Notes started per YM2610 channel type since power-on (to classify sounds; not part of the
 * machine state). */
typedef struct { uint32_t fm, ssg, adpcm_a, adpcm_b; } NgNotes;
extern NgNotes ng_notes;
/* Sound only (music menu): the 68000 stays stopped and the Z80 and YM2610 run on their own. The
 * machine is left in a state that is not the game's: save it before and restore it afterwards. */
void ng_sound_only_command(uint8_t command);
void ng_sound_only_advance(int64_t cycles_68k);

/* Generated audio (stereo, 16 bits, 55 555 Hz). Only stored if ng_audio_enabled != 0. */
extern int ng_audio_enabled;
#define NG_AUDIO_HZ 55555
uint32_t ng_audio_read(int16_t *dest, uint32_t max_samples);

void ng_bus_init(void);

#endif
