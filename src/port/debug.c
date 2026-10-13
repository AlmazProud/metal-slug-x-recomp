/* Debug mode (Phase 6A).
 *
 * Panel (F3):
 *   - host FPS, emulated frame and turbo;
 *   - game logic in Hz: 60 / (frames the last cycle took, profile byte); 30 Hz is normal and
 *     20 or 15 Hz is slowdown;
 *   - 68000 load in the frame: line starts at which it was NOT in the wait loop;
 *   - graph of the last 4 s (bar = load; red = frame of a slow cycle);
 *   - frames lost to slowdown in the last 10 s.
 * Keys: F6 advances one frame (while paused), Ctrl+1 / Ctrl+2 hide sprites / fix layer, Ctrl+3
 * sprite boxes, Ctrl+4 palette, Ctrl+5 memory (PgUp / PgDn / Home to move around).
 */
#include "debug.h"

#include <stdio.h>
#include <string.h>

#include "neogeo.h"
#include "port.h"
#include "profile.h"

#define HISTORY 600                          /* 10 s */
#define GRAPH 240                            /* 4 s */

static struct {
    int active, boxes, palette, memory;
    uint32_t mem_addr;                       /* offset into work RAM */
    uint8_t load[HISTORY], logic[HISTORY];
    unsigned n;                              /* frames recorded */
    Uint64 fps_since;
    int fps_frames;
    double fps;
    SDL_Texture *tex_palette;
} d;

static void clear_boxes(void)
{
    for (int i = 0; i < 381; i++) ng_dbg.box[i] = ng_dbg.box_current[i] = (NgBox){1, 0, 1, 0};
}

void debug_enable(int active)
{
    d.active = active;
    ng_dbg.active = active;
    ng_dbg.wait_start = PROFILE_WAIT_START;
    ng_dbg.wait_end = PROFILE_WAIT_END;
    if (!active) ng_dbg.hide = 0;
    clear_boxes();
    d.n = 0;
    const char *views = getenv("PD_DEBUG_VIEWS");     /* tests: "boxes,palette,memory" */
    if (active && views) {
        d.boxes = strstr(views, "boxes") != NULL;
        d.palette = strstr(views, "palette") != NULL;
        d.memory = strstr(views, "memory") != NULL;
    }
}

int debug_enabled(void) { return d.active; }

int debug_key(const SDL_KeyboardEvent *e)
{
    if (e->scancode == SDL_SCANCODE_F3) { debug_enable(!d.active); return DEBUG_HANDLED; }
    if (!d.active) return DEBUG_NONE;
    if (e->scancode == SDL_SCANCODE_F6) return DEBUG_STEP;
    if (e->mod & SDL_KMOD_CTRL) {
        switch (e->scancode) {
        case SDL_SCANCODE_1: ng_dbg.hide ^= NG_HIDE_SPRITES; return DEBUG_HANDLED;
        case SDL_SCANCODE_2: ng_dbg.hide ^= NG_HIDE_FIX; return DEBUG_HANDLED;
        case SDL_SCANCODE_3: d.boxes = !d.boxes; return DEBUG_HANDLED;
        case SDL_SCANCODE_4: d.palette = !d.palette; return DEBUG_HANDLED;
        case SDL_SCANCODE_5: d.memory = !d.memory; return DEBUG_HANDLED;
        default: break;
        }
    }
    if (d.memory) {
        if (e->scancode == SDL_SCANCODE_PAGEUP) { d.mem_addr = (d.mem_addr - 0x100) & 0xFF00; return DEBUG_HANDLED; }
        if (e->scancode == SDL_SCANCODE_PAGEDOWN) { d.mem_addr = (d.mem_addr + 0x100) & 0xFF00; return DEBUG_HANDLED; }
        if (e->scancode == SDL_SCANCODE_HOME) { d.mem_addr = 0; return DEBUG_HANDLED; }
    }
    return DEBUG_NONE;
}

void debug_frame(void)
{
    if (!d.active) return;
    int wait = ng_dbg.wait_lines;
    int load = 100 - wait * 100 / NG_LINES;
    unsigned i = d.n++ % HISTORY;
    d.load[i] = (uint8_t)(load < 0 ? 0 : load);
    d.logic[i] = ng.ram[PROFILE_RAM_FRAMES_PER_TICK];
}

/* ------------------------------------------------------------------ drawing */
static void text(SDL_Renderer *r, float x, float y, const char *s, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x, y, s);
}

static void backdrop(SDL_Renderer *r, float x, float y, float w, float h)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
    SDL_FRect f = {x, y, w, h};
    SDL_RenderFillRect(r, &f);
}

static void draw_boxes(SDL_Renderer *r, SDL_FRect j)
{
    float sx = j.w / NG_WIDTH, sy = j.h / NG_HEIGHT;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 381; i++) {
        const NgBox *c = &ng_dbg.box[i];
        if (c->x1 < c->x0) continue;
        if (c->y1 - c->y0 >= 200) continue;             /* background columns: full height */
        SDL_FRect f = {j.x + c->x0 * sx, j.y + c->y0 * sy, (c->x1 - c->x0 + 1) * sx, (c->y1 - c->y0 + 1) * sy};
        /* stable color per sprite */
        SDL_SetRenderDrawColor(r, (Uint8)(80 + (i * 97) % 176), (Uint8)(80 + (i * 57) % 176), (Uint8)(80 + (i * 31) % 176), 200);
        SDL_RenderRect(r, &f);
    }
}

static void draw_palette(SDL_Renderer *r, float x, float y, float side)
{
    if (!d.tex_palette) {
        d.tex_palette = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 64, 64);
        SDL_SetTextureScaleMode(d.tex_palette, SDL_SCALEMODE_NEAREST);
    }
    /* 4096 colors of the active bank: each row is 4 palettes of 16 colors. */
    const uint32_t *pens = ng.pen[ng.shadow] + ((uint32_t)ng.palette_bank << 12);
    SDL_UpdateTexture(d.tex_palette, NULL, pens, 64 * 4);
    backdrop(r, x - 4, y - 14, side + 8, side + 18);
    text(r, x, y - 11, "Palette (256 x 16)", 255, 210, 80);
    SDL_FRect f = {x, y, side, side};
    SDL_RenderTexture(r, d.tex_palette, NULL, &f);
}

static void draw_memory(SDL_Renderer *r, float x, float y)
{
    char b[96];
    const float L = 10;
    backdrop(r, x - 4, y - 4, 58 * 8 + 8, 18 * L + 6);
    snprintf(b, sizeof b, "RAM %06X  (PgUp/PgDn/Home)", 0x100000 + d.mem_addr);
    text(r, x, y, b, 255, 210, 80);
    for (int f = 0; f < 16; f++) {
        uint32_t a = d.mem_addr + (uint32_t)f * 16;
        int n = snprintf(b, sizeof b, "%06X ", 0x100000 + a);
        for (int k = 0; k < 16; k++)
            n += snprintf(b + n, sizeof b - (size_t)n, k == 8 ? "  %02X" : " %02X", ng.ram[(a + (uint32_t)k) & 0xFFFF]);
        text(r, x, y + (f + 2) * L, b, 220, 220, 220);
    }
}

void debug_draw(SDL_Renderer *r, SDL_FRect game, int w, int h, int turbo)
{
    Uint64 now = SDL_GetTicksNS();
    d.fps_frames++;
    if (now - d.fps_since >= 1000000000ULL) {
        d.fps = d.fps_frames * 1e9 / (double)(now - d.fps_since);
        d.fps_since = now;
        d.fps_frames = 0;
    }
    if (!d.active) return;
    if (d.boxes) draw_boxes(r, game);

    int k = SDL_max(1, SDL_min(w / 640, h / 480));
    SDL_SetRenderScale(r, (float)k, (float)k);
    float vw = (float)w / k, vh = (float)h / k;
    const float L = 10;
    char b[128];
    float x = 8, y = 8;
    backdrop(r, 4, 4, 51 * 8, 12 * L + 48);
    text(r, x, y, "DEBUG  F3 close  F6 step  P pause", 255, 210, 80);

    snprintf(b, sizeof b, "FPS %5.1f  frame %llu  turbo x%d", d.fps, (unsigned long long)ng.frames, 1 << turbo);
    text(r, x, y + L, b, 220, 220, 220);

    unsigned last = (d.n + HISTORY - 1) % HISTORY;
    int l = d.n ? d.logic[last] : 0;
    if (l >= 1 && l <= 8) snprintf(b, sizeof b, "Logic %4.1f Hz (%d frames per cycle)", 60.0 / l, l);
    else snprintf(b, sizeof b, "Logic  -   (outside the game)");
    if (l > 2 && l <= 8) text(r, x, y + 2 * L, b, 255, 90, 90);
    else text(r, x, y + 2 * L, b, 120, 230, 120);

    /* lost frames: a frame of an l-frame cycle loses (l - 2) / l */
    double lost = 0;
    unsigned count = d.n < HISTORY ? d.n : HISTORY;
    for (unsigned i = 0; i < count; i++)
        if (d.logic[i] > 2 && d.logic[i] <= 8) lost += (d.logic[i] - 2) / (double)d.logic[i];
    /* The logic runs every 2 frames: the load of a single frame alternates; the mean and the
       peak of the last half second are shown. */
    int sum = 0, peak = 0, m = d.n < 30 ? (int)d.n : 30;
    for (int i = 0; i < m; i++) {
        int c = d.load[(d.n - 1 - (unsigned)i) % HISTORY];
        sum += c;
        if (c > peak) peak = c;
    }
    snprintf(b, sizeof b, "CPU 68000 mean %3d%% peak %3d%%", m ? sum / m : 0, peak);
    text(r, x, y + 3 * L, b, 220, 220, 220);
    snprintf(b, sizeof b, "Lost frames (10 s): %4.1f%%", count ? 100.0 * lost / count : 0.0);
    text(r, x, y + 4 * L, b, 220, 220, 220);

    /* graph: load per frame */
    float gx = x, gy = y + 5 * L + 2, gh = 40;
    SDL_SetRenderDrawColor(r, 60, 60, 60, 255);
    SDL_FRect frame_rect = {gx - 1, gy - 1, GRAPH * 1.5f + 2, gh + 2};
    SDL_RenderRect(r, &frame_rect);
    unsigned from = d.n > GRAPH ? d.n - GRAPH : 0;
    for (unsigned c = from; c < d.n; c++) {
        unsigned i = c % HISTORY;
        float height = d.load[i] * gh / 100;
        int slow = d.logic[i] > 2 && d.logic[i] <= 8;
        SDL_SetRenderDrawColor(r, slow ? 230 : 80, slow ? 70 : 200, slow ? 70 : 90, 255);
        SDL_FRect bar = {gx + (c - from) * 1.5f, gy + gh - height, 1.5f, height};
        SDL_RenderFillRect(r, &bar);
    }
    float ty = gy + gh + 4;
    snprintf(b, sizeof b, "Ctrl+1 sprites %s  Ctrl+2 fix %s", (ng_dbg.hide & NG_HIDE_SPRITES) ? "NO" : "yes",
             (ng_dbg.hide & NG_HIDE_FIX) ? "NO" : "yes");
    text(r, x, ty, b, 170, 170, 170);
    snprintf(b, sizeof b, "Ctrl+3 boxes %s  Ctrl+4 palette %s  Ctrl+5 RAM %s", d.boxes ? "yes" : "no",
             d.palette ? "yes" : "no", d.memory ? "yes" : "no");
    text(r, x, ty + L, b, 170, 170, 170);
    /* the loaded copy (neogeo/roms.c) and how the 68000 runs it (main.c) */
    snprintf(b, sizeof b, "%.50s", ng_roms.info.description);
    text(r, x, ty + 2 * L, b, 140, 180, 255);
    snprintf(b, sizeof b, "68000: %s", machine_recompiled() ? "recompiled program" : "interpreter");
    text(r, x, ty + 3 * L, b, 140, 180, 255);

    if (d.palette) draw_palette(r, vw - 64 * 3 - 8, 22, 64 * 3);
    if (d.memory) draw_memory(r, 8, vh - 18 * L - 8);
    SDL_SetRenderScale(r, 1, 1);
}
