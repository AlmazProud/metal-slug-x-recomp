/* LSPC: VRAM, registers, palette and per-line drawing of sprites and the fix layer.
 *
 * VRAM (words):
 *   0000-6FFF  SCB1: 64 words per sprite (32 tiles: low code, attributes)
 *   7000-74FF  fix layer: 40 columns x 32 rows, column-major; palette in bits 15-12
 *   8000-81FF  SCB2: shrink (bits 11-8 horizontal, 7-0 vertical)
 *   8200-83FF  SCB3: Y (bits 15-7), chained (bit 6), height in tiles (bits 5-0)
 *   8400-85FF  SCB4: X (bits 15-7)
 *   8600-86FF  per-line sprite lists (written by the LSPC itself, even/odd)
 *
 * Timing: at the start of each line the previous one is drawn (with the VRAM and palette of
 * that moment) and the sprite list of the current one is built: up to 96 of the 381 sprites.
 *
 * The graphics are read directly in DotEmu's unpacked format (4 bpp in rows,
 * low nibble = left pixel), bit-for-bit equivalent to the C ROMs (docs/ROM_MAP.md).
 */
#include "neogeo.h"

#include <string.h>

#define MAX_SPRITES 381
#define MAX_PER_LINE 96

/* Horizontal shrink: which tile columns are drawn for each value (hardware). */
static const uint16_t ZOOM_X[16] = {
    0x0080, 0x0880, 0x0888, 0x2888, 0x288a, 0x2a8a, 0x2aaa, 0xaaaa,
    0xaaea, 0xbaea, 0xbaeb, 0xbbeb, 0xbbef, 0xfbef, 0xfbff, 0xffff,
};

/* ------------------------------------------------------------------ palette */
/* DAC of each component: 5 resistors (3900, 2200, 1000, 470 and 220 ohm, from the least to the
 * most significant bit) in parallel to the output; the "dark" bit adds 8200 ohm to ground and the
 * shadow 150 ohm. Each bit contributes the voltage of the divider formed by its resistor (to Vcc)
 * against the rest (to ground); the scale is set so that normal white is 255. */
static uint8_t level[32][4];                 /* [5-bit value][normal, dark, shadow, both] */

static void compute_weights(double load, double scale, double weights[5])
{
    static const double R[5] = {3900, 2200, 1000, 470, 220};
    for (int n = 0; n < 5; n++) {
        double g_gnd = load > 0 ? 1.0 / load : 1e-12, g_vcc = 1e-12;
        for (int j = 0; j < 5; j++) {
            if (j == n) g_vcc += 1.0 / R[j];
            else g_gnd += 1.0 / R[j];
        }
        double r_gnd = 1.0 / g_gnd, r_vcc = 1.0 / g_vcc;
        double v = 255.0 * r_gnd / (r_vcc + r_gnd);
        weights[n] = (v < 0 ? 0 : v > 255 ? 255 : v) * scale;
    }
}

static void build_levels(void)
{
    double p[4][5];
    compute_weights(0, 1.0, p[0]);
    double sum = 0;
    for (int n = 0; n < 5; n++) sum += p[0][n];
    double scale = 255.0 / sum;
    const double loads[4] = {0, 8200, 150, 1.0 / (1.0 / 8200 + 1.0 / 150)};
    for (int k = 0; k < 4; k++) compute_weights(loads[k], scale, p[k]);
    for (int i = 0; i < 32; i++)
        for (int k = 0; k < 4; k++) {
            double s = 0;
            for (int n = 0; n < 5; n++) if (i >> n & 1) s += p[k][n];
            level[i][k] = (uint8_t)(int)(s + 0.5);
        }
}

void ng_palette_write(uint32_t i, uint16_t v)
{
    ng.palette[i] = v;
    int dark = v >> 15;
    int r = ((v >> 14) & 1) | ((v >> 7) & 0x1E);
    int g = ((v >> 13) & 1) | ((v >> 3) & 0x1E);
    int b = ((v >> 12) & 1) | ((v << 1) & 0x1E);
    for (int s = 0; s < 2; s++) {
        int k = dark + 2 * s;
        ng.pen[s][i] = 0xFF000000u | (uint32_t)level[r][k] << 16 | (uint32_t)level[g][k] << 8 | level[b][k];
    }
}

/* ------------------------------------------------------------------ VRAM and registers */
static void set_addr(uint16_t v)
{
    ng.vram_addr = (v & 0x8000) ? (v & 0x87FF) : v;
    ng.vram_buffer = ng.vram[ng.vram_addr];           /* the read happens when the address is set */
}

static void irq2_schedule(void)
{
    ng.irq2_when = ng_cpu.cycles + ((int64_t)ng.irq2_counter + 1) * 2;   /* pixel clock = 6 MHz */
}

uint16_t ng_video_read(uint32_t reg)
{
    switch (reg) {
    case 0: case 1: return ng.vram_buffer;
    case 2: return ng.vram_mod;
    default: {
        /* Vertical counter: 0x100 + vpos, from 0xF8 to 0x1FF. */
        int64_t t = ng_cpu.cycles - ng.frame_start;
        int vpos = (int)(t / NG_CYCLES_PER_LINE);
        if (vpos >= NG_LINES) vpos = NG_LINES - 1;
        int v = vpos + 0x100;
        if (v >= 0x200) v -= NG_LINES;
        return (uint16_t)(v << 7 | (ng.anim_counter & 7));
    }
    }
}

void ng_video_write(uint32_t reg, uint16_t v)
{
    switch (reg) {
    case 0: set_addr(v); break;
    case 1:
        ng.vram[ng.vram_addr] = v;
        set_addr((uint16_t)((ng.vram_addr & 0x8000) | ((ng.vram_addr + ng.vram_mod) & 0x7FFF)));
        break;
    case 2: ng.vram_mod = v; break;
    case 3:
        ng.anim_speed = (uint8_t)(v >> 8);
        ng.anim_disabled = (v >> 3) & 1;
        ng.irq2_control = v & 0xF0;
        break;
    case 4: ng.irq2_counter = (ng.irq2_counter & 0xFFFF) | (uint32_t)v << 16; break;
    case 5:
        ng.irq2_counter = (ng.irq2_counter & 0xFFFF0000u) | v;
        if (ng.irq2_control & 0x20) irq2_schedule();           /* relative load */
        break;
    case 6:
        if (v & 1) ng.irq3 = 0;
        if (v & 2) ng.irq_raster = 0;
        if (v & 4) {
            ng.irq_vblank = 0;
            if (ng_on_vblank_ack) ng_on_vblank_ack();
        }
        ng_irq_update();
        break;
    default: break;                                            /* 7: PAL only */
    }
}

void ng_video_anim_tick(void)
{
    if (ng.anim_frames == 0) {
        ng.anim_frames = ng.anim_speed;
        ng.anim_counter++;
    } else {
        ng.anim_frames--;
    }
}

/* ------------------------------------------------------------------ sprites */
static int on_line(int line, int y, int rows)
{
    return rows == 0 || rows >= 0x20 || ((line - y) & 0x1FF) < rows * 0x10;
}

static void build_list(int line)
{
    uint16_t *list = &ng.vram[(line & 1) ? 0x8680 : 0x8600];
    int y = 0, rows = 0, n = 0;
    for (int s = 0; s < MAX_SPRITES && n < MAX_PER_LINE; s++) {
        uint16_t ctl = ng.vram[0x8200 | s];
        if (!(ctl & 0x40)) {
            y = 0x200 - (ctl >> 7);
            rows = ctl & 0x3F;
        }
        if (rows == 0 || !on_line(line, y, rows)) continue;
        list[n++] = (uint16_t)s;
    }
    /* the rest set to 0, with one extra entry */
    for (int i = n; i <= MAX_PER_LINE && (list + i) < ng.vram + 0x8800; i++) list[i] = 0;
}

/* Pixel `col` (0-15, left to right on screen) of a tile row. In the DotEmu file each 8-pixel
 * half is stored in reverse order relative to the screen (bit 7 - x of the C ROM planes is
 * pixel x of the half, see docs/ROM_MAP.md), hence col ^ 7. */
static inline uint8_t sprite_pixel(uint32_t code, int row, int col)
{
    col ^= 7;
    const uint8_t *t = ng_roms.tiles + (size_t)code * 128 + row * 8 + (col >> 1);
    return (col & 1) ? (*t >> 4) : (*t & 0x0F);
}

static void draw_list(uint32_t *buf, const uint32_t *pens, int line, const uint16_t *list, int last);

static void draw_sprites(uint32_t *buf, const uint32_t *pens, int line)
{
    const uint16_t *list = &ng.vram[(line & 1) ? 0x8680 : 0x8600];
    int last;
    for (last = MAX_PER_LINE - 1; last >= 0; last--)
        if (list[last] != 0) break;
    /* The list ends in zeros, but a 0 may be sprite 0: one more is drawn. */
    if (last != MAX_PER_LINE - 1) last++;
    draw_list(buf, pens, line, list, last);
}

/* Draws sprites list[0..last] on the line (chained sprites follow the previous one in the list). */
static void draw_list(uint32_t *buf, const uint32_t *pens, int line, const uint16_t *list, int last)
{

    int x = 0, y = 0, rows = 0, zoom_x = 0, zoom_y = 0;
    for (int i = 0; i <= last; i++) {
        int s = list[i] & 0x1FF;
        uint16_t ctl_y = ng.vram[0x8200 | s];
        uint16_t ctl_zoom = ng.vram[0x8000 | s];
        if (ctl_y & 0x40) {                           /* chained: continues to the right */
            x = (x + zoom_x + 1) & 0x1FF;
            zoom_x = (ctl_zoom >> 8) & 0x0F;
        } else {
            y = 0x200 - (ctl_y >> 7);
            x = ng.vram[0x8400 | s] >> 7;
            zoom_y = ctl_zoom & 0xFF;
            zoom_x = (ctl_zoom >> 8) & 0x0F;
            rows = ctl_y & 0x3F;
        }
        if (x >= 0x140 && x <= 0x1F0) continue;       /* off screen */
        if (!on_line(line, y, rows)) continue;

        int spr_line = (line - y) & 0x1FF;
        int zoom_line = spr_line & 0xFF;
        int invert = (spr_line >> 8) & 1;
        if (invert) zoom_line ^= 0xFF;
        if (rows > 0x20) {                            /* sprites taller than 32 tiles: repetition */
            zoom_line %= (zoom_y + 1) << 1;
            if (zoom_line > zoom_y) {
                zoom_line = ((zoom_y + 1) << 1) - 1 - zoom_line;
                invert = !invert;
            }
        }
        uint8_t yt = ng_roms.zoom[zoom_y << 8 | zoom_line];
        int tile_row = yt & 0x0F, tile = yt >> 4;
        if (invert) { tile_row ^= 0x0F; tile ^= 0x1F; }

        uint32_t addr = (uint32_t)s << 6 | (uint32_t)tile << 1;
        uint16_t attr = ng.vram[addr + 1];
        uint32_t code = ((uint32_t)(attr << 12) & 0xF0000) | ng.vram[addr];
        if (!ng.anim_disabled) {
            if (attr & 0x08) code = (code & ~7u) | (ng.anim_counter & 7);
            else if (attr & 0x04) code = (code & ~3u) | (ng.anim_counter & 3);
        }
        if (attr & 0x02) tile_row ^= 0x0F;            /* vertical flip */
        code &= 0x7FFFF;                              /* 64 MB addressable */
        if (code >= ng_roms.n_tiles) continue;        /* outside the ROM: transparent */

        if (ng_dbg.active) {                          /* sprite box (debug mode) */
            NgBox *k = &ng_dbg.box_current[s];
            int x0 = x > 0x1F0 ? x - 0x200 : x, x1 = x0 + zoom_x;
            int yv = line - NG_FIRST_VISIBLE_LINE;
            if (k->x1 < k->x0) *k = (NgBox){(int16_t)x0, (int16_t)x1, (int16_t)yv, (int16_t)yv};
            else {
                if (x0 < k->x0) k->x0 = (int16_t)x0;
                if (x1 > k->x1) k->x1 = (int16_t)x1;
                if (yv < k->y0) k->y0 = (int16_t)yv;
                if (yv > k->y1) k->y1 = (int16_t)yv;
            }
        }
        const uint32_t *p = pens + ((attr >> 8) << 4);
        uint16_t map = ZOOM_X[zoom_x];
        int hflip = attr & 0x01;
        int wrapped = x > 0x1F0;                      /* starts left of the screen */
        int px = x;
        for (int col = 0; col < 16 && map; col++, map = (uint16_t)(map << 1)) {
            if (!(map & 0x8000)) continue;
            int pos = px++;
            if (wrapped && pos < 0x200) continue;
            int dest = pos & 0x1FF;
            uint8_t c = sprite_pixel(code, tile_row, hflip ? 15 - col : col);
            if (c && dest < NG_WIDTH) buf[dest] = p[c];
        }
    }
}

/* ------------------------------------------------------------------ fix layer */
static void draw_fix(uint32_t *buf, const uint32_t *pens, int line)
{
    const uint8_t *gfx = ng.cart_fix ? ng_roms.fix_game : ng_roms.fix_bios;
    int row = line >> 3, sub = line & 7;
    for (int col = 0; col < 40; col++) {
        uint16_t v = ng.vram[0x7000 | (col << 5) | (row & 0x1F)];
        uint32_t code = v & 0x0FFF;
        const uint32_t *p = pens + ((v >> 12) << 4);
        const uint8_t *t = gfx + code * 32 + sub * 4;
        for (int k = 0; k < 4; k++) {
            uint8_t b = t[k];
            if (b & 0x0F) buf[col * 8 + 2 * k] = p[b & 0x0F];
            if (b & 0xF0) buf[col * 8 + 2 * k + 1] = p[b >> 4];
        }
    }
}

/* ------------------------------------------------------------------ lines */
static void draw_line(int line)
{
    if (line < NG_FIRST_VISIBLE_LINE || line >= NG_VBLANK_LINE) return;
    uint32_t *buf = ng.screen[line - NG_FIRST_VISIBLE_LINE];
    const uint32_t *pens = ng.pen[ng.shadow] + ((uint32_t)ng.palette_bank << 12);
    uint32_t background = pens[0xFFF];
    for (int i = 0; i < NG_WIDTH; i++) buf[i] = background;
    if (!(ng_dbg.hide & NG_HIDE_SPRITES)) draw_sprites(buf, pens, line);
    if (!(ng_dbg.hide & NG_HIDE_FIX)) draw_fix(buf, pens, line);
}

/* Phase 6C: redraws only sprites [first, last] with the current VRAM and palette, over a
   transparent background (0), without touching the machine state. Used to show game graphics
   in the menus (the title screen logo). */
void ng_video_capture_sprites(uint32_t *dest, int first, int last)
{
    const uint32_t *pens = ng.pen[ng.shadow] + ((uint32_t)ng.palette_bank << 12);
    uint16_t list[MAX_SPRITES];
    for (int y = 0; y < NG_HEIGHT; y++) {
        int line = y + NG_FIRST_VISIBLE_LINE, n = 0, yc = 0, rows = 0;
        for (int s = 0; s < MAX_SPRITES; s++) {
            uint16_t ctl = ng.vram[0x8200 | s];
            if (!(ctl & 0x40)) { yc = 0x200 - (ctl >> 7); rows = ctl & 0x3F; }
            if (s < first || s > last || rows == 0 || !on_line(line, yc, rows)) continue;
            list[n++] = (uint16_t)s;
        }
        uint32_t *buf = dest + (size_t)y * NG_WIDTH;
        memset(buf, 0, NG_WIDTH * sizeof *buf);
        if (n) draw_list(buf, pens, line, list, n - 1);
    }
}

void ng_video_line_start(int line)
{
    if (line != 0) draw_line(line - 1);
    build_list(line);
}

void ng_video_init(void)
{
    static int levels_ready;
    if (!levels_ready) { build_levels(); levels_ready = 1; }
    memset(ng.vram, 0, sizeof ng.vram);
    for (uint32_t i = 0; i < 0x2000; i++) ng_palette_write(i, 0);
    ng.vram_addr = ng.vram_buffer = ng.vram_mod = 0;
    ng.anim_speed = ng.anim_disabled = ng.anim_counter = ng.anim_frames = 0;
    ng.irq2_control = 0;
    ng.irq2_counter = 0;
    ng.irq2_when = -1;
}
