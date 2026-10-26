/* Boss health bar (bossbar.h). */
#include "bossbar.h"

#include <stdio.h>
#include <string.h>

#include "lang.h"
#include "neogeo.h"
#include "profile.h"
#include "ui_font.h"

#define MIN_LIFE 1000
#define MAX_LIFE 0x7000                   /* 0x7FFF, 0xFFF6: indestructible */
#define TRACKED 8
#define LINGER 90                         /* frames the bar stays after the enemy dies */
#define MIN_AGE 30                        /* a part freed with its parent is not a fight */

typedef struct {
    uint32_t addr;
    const ProfileBoss *boss;
    int full, life, engaged, gone;        /* gone: frames since it died or left the list */
    int age;                              /* frames followed (a hit counts after MIN_AGE) */
    uint64_t last_hit;
} Enemy;

static Enemy e[TRACKED];
static float shown = -1, trail = -1;      /* drawn fraction and the damage trail */
static int shown_slot = -1;
static uint64_t shown_at;                 /* emulated frame when the bar appeared */
static char describe[96];

int bossbar_supported(void) { return game_profile.object_list && profile_supported(); }

void bossbar_reset(void)
{
    memset(e, 0, sizeof e);
    shown = trail = -1;
    shown_slot = -1;
}

static uint32_t l32(uint32_t a) { return (uint32_t)ng.ram[a & 0xFFFF] << 24 | (uint32_t)ng.ram[(a + 1) & 0xFFFF] << 16 |
                                         (uint32_t)ng.ram[(a + 2) & 0xFFFF] << 8 | ng.ram[(a + 3) & 0xFFFF]; }
static int w16(uint32_t a) { return ng.ram[a & 0xFFFF] << 8 | ng.ram[(a + 1) & 0xFFFF]; }

/* The boss whose code runs the object, or NULL. */
static const ProfileBoss *boss_of(uint32_t routine)
{
    for (int i = 0; i < game_profile.n_bosses; i++) {
        const ProfileBoss *b = &game_profile.bosses[i];
        if (b->mission && game_profile.mission_now && ng.ram[game_profile.mission_now & 0xFFFF] + 1 != b->mission)
            continue;
        if (routine >= b->code_lo && routine <= b->code_hi) return b;
    }
    return NULL;
}

void bossbar_frame(void)
{
    if (!bossbar_supported()) return;
    int seen[TRACKED] = {0};
    uint32_t head = game_profile.object_list, a = l32(head + 4);
    for (int n = 0; a && a != head && a >= 0x100000 && a < 0x10F000 && n < 400; n++, a = l32(a + 4)) {
        int life = w16(a + game_profile.object_life);
        int dead = ng.ram[(a + game_profile.object_dead) & 0xFFFF] &
                   (game_profile.object_dead_mask ? game_profile.object_dead_mask : 0x80);
        if (game_profile.boss_life_signed && life >= 0x8000) life = 0;       /* below 0: defeated */
        int min_life = game_profile.boss_min_life ? game_profile.boss_min_life : MIN_LIFE;
        int k = -1;
        for (int i = 0; i < TRACKED; i++)
            if (e[i].addr == a && !e[i].gone) k = i;
        if (k < 0) {
            const ProfileBoss *b = boss_of(l32(a));            /* only the bosses: other enemies have no bar */
            if (!b || dead || life < min_life || life >= MAX_LIFE) continue;
            /* a part with its parent's life (parts copy it every frame): the parent is the enemy */
            uint32_t parent = l32(a + 0xC);
            if (parent != head && parent >= 0x100000 && parent < 0x10F000 &&
                w16(parent + game_profile.object_life) == life)
                continue;
            for (int i = 0; i < TRACKED && k < 0; i++)
                if (!e[i].addr || e[i].gone > LINGER) k = i;
            if (k < 0) continue;
            e[k] = (Enemy){a, b, life, life, 0, 0, 0, 0};
        }
        seen[k] = 1;
        Enemy *x = &e[k];
        if (life > x->full && life < MAX_LIFE) x->full = life;       /* a part that comes back */
        if (life < x->life && x->age >= MIN_AGE) { x->engaged = 1; x->last_hit = ng.frames; }
        x->age++;
        x->life = life;
        if (x->boss->life > x->full) x->full = x->boss->life;         /* known full life (a loaded game) */
        if (dead) { x->life = 0; if (!x->gone) x->gone = 1; }
    }
    for (int i = 0; i < TRACKED; i++) {
        if (!e[i].addr) continue;
        if (!seen[i] && !e[i].gone) { e[i].gone = 1; e[i].life = e[i].engaged ? 0 : e[i].life; }
        else if (e[i].gone) e[i].gone++;
        if (e[i].gone > LINGER + 600) e[i] = (Enemy){0};
    }
}

/* The enemy whose bar is shown: the one hit most recently, still alive or just dead. */
static int current(void)
{
    int best = -1;
    for (int i = 0; i < TRACKED; i++) {
        if (!e[i].addr || !e[i].engaged || e[i].gone > LINGER) continue;
        if (best < 0 || e[i].last_hit > e[best].last_hit) best = i;
    }
    return best;
}

void bossbar_test_engage(void)
{
    for (int i = 0; i < TRACKED; i++)
        if (e[i].addr && !e[i].gone) { e[i].engaged = 1; e[i].last_hit = ng.frames; }
}

const char *bossbar_describe(void)
{
    int k = current();
    if (k < 0) describe[0] = 0;
    else snprintf(describe, sizeof describe, "%s %06X %d/%d%s", e[k].boss->name ? e[k].boss->name : "boss",
                  (unsigned)e[k].addr, e[k].life, e[k].full, e[k].gone ? " dead" : "");
    return describe;
}

static void fill(SDL_Renderer *r, float x, float y, float w, float h, uint32_t c, int a)
{
    SDL_SetRenderDrawColor(r, (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, (Uint8)a);
    SDL_FRect f = {x, y, w, h};
    SDL_RenderFillRect(r, &f);
}

/* The frame's yellow stripes, across the bar: slanted, about one bar width wide, one every 2.3
   widths (the sprite turned upright). */
static void stripes(SDL_Renderer *r, float x, float y, float w, float h, float alpha)
{
    SDL_Rect old;
    int clipped = SDL_RenderClipEnabled(r);
    SDL_GetRenderClipRect(r, &old);
    SDL_Rect clip = {(int)SDL_ceilf(x), (int)SDL_ceilf(y), (int)(x + w) - (int)SDL_ceilf(x), (int)(y + h) - (int)SDL_ceilf(y)};
    SDL_SetRenderClipRect(r, &clip);
    SDL_FColor c = {1.0f, 188 / 255.0f, 61 / 255.0f, alpha};
    float period = 2.32f * w, band = 0.95f * w, slant = 0.65f * w;
    for (float sy = y + 0.4f * w - period; sy < y + h; sy += period) {
        SDL_Vertex v[4] = {
            {{x, sy}, c, {0, 0}}, {{x, sy + band}, c, {0, 0}},
            {{x + w, sy + band + slant}, c, {0, 0}}, {{x + w, sy + slant}, c, {0, 0}},
        };
        static const int idx[6] = {0, 1, 2, 0, 2, 3};
        SDL_RenderGeometry(r, NULL, v, 4, idx, 6);
    }
    SDL_SetRenderClipRect(r, clipped ? &old : NULL);
}

void bossbar_draw(SDL_Renderer *r, SDL_FRect game)
{
    if (!bossbar_supported()) return;
    int k = current();
    if (k < 0) { shown = trail = -1; shown_slot = -1; return; }
    float frac = e[k].full ? (float)e[k].life / (float)e[k].full : 0;
    if (k != shown_slot || shown < 0) { shown = trail = frac; shown_slot = k; shown_at = ng.frames; }
    shown += (frac - shown) * 0.35f;                     /* quick to the value */
    if (trail < shown) trail = shown;
    else trail -= SDL_max(0.002f, (trail - shown) * 0.05f);   /* the damage trail catches up slowly */
    if (trail < shown) trail = shown;
    /* the sprite (a friend of the user's design, drawn from its measures) turned upright, at the
       right edge under player 2's score: a hazard-stripe frame 57 sprite pixels wide with a glossy
       red fill inset that empties from the top; in game pixels (320 x 224), scaled */
    float s = game.h / 224.0f;
    /* appearing: in 30 frames (0.5 s) it slides in from the right edge, fades in and fills up (ease-out) */
    float t = SDL_min(1.0f, (float)(ng.frames - shown_at) / 30), in = 1 - (1 - t) * (1 - t) * (1 - t);
    float w = 9 * s, h = 132 * s, x = game.x + game.w - w - 10 * s + (1 - in) * 16 * s, y = game.y + 50 * s;
    float u = w / 57;                                    /* one sprite pixel */
    float alpha = (e[k].gone ? SDL_max(0.0f, 1.0f - (float)e[k].gone / LINGER) : 1.0f) * in;
    float full = shown * in, behind = trail * in;
    int a = (int)(alpha * 255);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    fill(r, x, y, w, h, 0x1A1A1F, a);
    stripes(r, x, y, w, h, alpha);
    /* the fill: 5 / 7 sprite pixels in from the left / right, 5 / 8 from the top / bottom */
    float fx = x + 5 * u, fy = y + 5 * u, fw = w - 12 * u, fh = h - 13 * u;
    float lost = fh * (1 - full), taken = fh * (behind - full);
    fill(r, fx, fy, fw, lost, 0x000000, (int)(alpha * 150));                   /* life lost */
    fill(r, fx, fy + lost - taken, fw, taken, 0xFFC8BE, (int)(alpha * 215));  /* damage just taken */
    /* the red's bands, left to right (46 in the sprite: its top to bottom) */
    static const struct { int rows; uint32_t c; } band[] = {
        {1, 0xFF6E62}, {5, 0xFF5D50}, {2, 0xFF220E}, {16, 0xFF0000}, {4, 0xE10000},
        {2, 0xB10001}, {4, 0x890017}, {2, 0x76071E}, {10, 0x710920},
    };
    float bx = fx;
    for (size_t i = 0; i < sizeof band / sizeof band[0]; i++) {
        float bw = fw * band[i].rows / 46;
        fill(r, bx, fy + lost, bw, fh - lost, band[i].c, a);
        bx += bw;
    }
    /* the boss's name, in the menu fonts at the output's pixel size */
    const char *label = e[k].boss->name ? e[k].boss->name : T("BOSS");
    float size = 8 * s;
    SDL_Color c = {0xFF, 0xBC, 0x3D, (Uint8)a};            /* the stripes' yellow */
    SDL_Color shadow = {0, 0, 0, (Uint8)(alpha * 200)};
    /* written top to bottom with the letters upright, one under another (as East Asian vertical
       text), to the left of the bar and from its top; smaller when the name is longer than the bar */
    int n = (int)strlen(label);
    float step = SDL_min(9.5f * s, h / (n > 0 ? n : 1)), size2 = SDL_min(size, step / 1.15f);
    float xm = x - 6 * s, ym = y + step / 2;
    for (int i = 0; i < n; i++, ym += step) {
        if (label[i] == ' ') continue;
        char ch[2] = {label[i], 0};
        ui_text_centered(r, UI_STENCIL, size2, 0, xm + 0.6f * s, ym + 0.6f * s, shadow, ch);
        ui_text_centered(r, UI_STENCIL, size2, 0, xm, ym, c, ch);
    }
}
