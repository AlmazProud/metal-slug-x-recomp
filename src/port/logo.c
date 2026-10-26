/* Title screen logo, taken from the game itself (from the user's copy).
 *
 * On the attract-mode title screen (frame ~1150 after power-on with empty SRAM), the logo is
 * sprites 41-124 (shadow, "METAL SLUG", the X and "SUPER VEHICLE-001") plus 380 ("TM"); the red
 * background is sprites 21-40 (src/port/profile.h). Only those sprites are redrawn with
 * ng_video_capture_sprites, the rectangle with content is cropped and saved as PNG in the
 * user's folder: it is generated only the first time. The logo never goes into the repository.
 */
#include "logo.h"

#include <stdlib.h>
#include <string.h>

#include "dispatcher.h"
#include "neogeo.h"
#include "profile.h"

static SDL_Texture *tex;

SDL_Texture *logo_texture(void) { return tex; }

void logo_destroy(void)
{
    if (tex) SDL_DestroyTexture(tex);
    tex = NULL;
}

static uint32_t sprite_tile(int s)
{
    return ((uint32_t)(ng.vram[(s << 6) + 1] << 12) & 0xF0000) | ng.vram[s << 6];
}

/* Runs the freshly powered-on machine up to the title and captures the logo; NULL if it does not appear. */
static SDL_Surface *generate(void)
{
    static uint32_t a[NG_HEIGHT][NG_WIDTH], b[NG_HEIGHT][NG_WIDTH];
    int wait = -1, found = 0;
    ng_power_on();
    for (int f = 0; f < (int)game_profile.logo_max_frames && !found; f++) {
        ng_run_frame();
        if (wait < 0 && sprite_tile(game_profile.logo_tm) == game_profile.logo_tm_tile) wait = 30;   /* let it finish sliding in */
        if (wait < 0 || wait-- > 0) continue;
        /* Then the first frame that draws both the logo and the TM (Metal Slug 5's title draws
           its sprites every other frame). */
        ng_video_capture_sprites(&a[0][0], game_profile.logo_first, game_profile.logo_last);
        ng_video_capture_sprites(&b[0][0], game_profile.logo_tm, game_profile.logo_tm);
        int has_a = 0, has_b = 0;
        for (int y = 0; y < NG_HEIGHT; y++)
            for (int x = 0; x < NG_WIDTH; x++) { has_a |= a[y][x] != 0; has_b |= b[y][x] != 0; }
        found = has_a && has_b;
    }
    if (!found) return NULL;
    int x0 = NG_WIDTH, y0 = NG_HEIGHT, x1 = -1, y1 = -1;
    for (int y = 0; y < NG_HEIGHT; y++)
        for (int x = 0; x < NG_WIDTH; x++) {
            if (b[y][x]) a[y][x] = b[y][x];
            if (!a[y][x]) continue;
            a[y][x] |= 0xFF000000u;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    if (x1 < 0) return NULL;
    SDL_Surface *s = SDL_CreateSurface(x1 - x0 + 1, y1 - y0 + 1, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return NULL;
    for (int y = y0; y <= y1; y++)
        memcpy((uint8_t *)s->pixels + (size_t)(y - y0) * s->pitch, &a[y][x0], (size_t)(x1 - x0 + 1) * 4);
    return s;
}

int logo_prepare(SDL_Renderer *r, const char *cache)
{
    SDL_Surface *s = SDL_LoadPNG(cache);
    if (!s) {
        /* The machine goes back exactly to how it was (full state, SRAM included). */
        size_t size;
        uint8_t *state = ng_state_save(&size);
        int audio = ng_audio_enabled, turbo = cpu_turbo;
        void (*hook)(uint8_t) = ng_on_sound_command;
        ng_audio_enabled = 0;
        ng_on_sound_command = NULL;
        cpu_turbo = 0;                /* at the original speed: with the turbo the title's sprites differ */
        s = generate();
        cpu_turbo = turbo;
        ng_audio_enabled = audio;
        ng_on_sound_command = hook;
        if (state) { ng_state_load(state, size); free(state); }
        if (s) SDL_SavePNG(s, cache);
    }
    if (!s) return -1;
    tex = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    if (!tex) return -1;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_PIXELART);
    return 0;
}
