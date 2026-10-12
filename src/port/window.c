/* Windowed mode (Phase 5): video, audio, 2-player controls, menu, save states.
 * Phase 6C: start screen and menus drawn with the game's font (menu.c, font.c).
 *
 * Fixed keys: Esc = pause menu, P = pause, F5 / F9 = save / load state in the active slot,
 * F11 or Alt+Enter = fullscreen, F12 = screenshot, F2 = test button (BIOS service menu:
 * difficulty, lives...), F3 = debug mode (debug.c). On the gamepad: Guide (or Back + Start)
 * opens the menu.
 *
 * Autofire: while held, button A is pressed and released automatically with a period of
 * 6, 8 or 12 frames. Measured in game (pistol sound command F7): the game does not fire more
 * than 10 times per second even if the pattern is faster, so 6 frames is the useful maximum.
 * The rest of the controls are configured in the menu or in mslugx.ini (config.c).
 *
 * Pacing (Phase 7A): without V-sync, one frame every 16.896 ms exactly (59.1856 Hz, the board).
 * With V-sync:
 *   - ~60 Hz monitor (58-62): one frame per refresh; the game runs at the monitor's rate
 *     (1.4 % faster at 60 Hz) and audio is played back at the same ratio
 *     (SDL_SetAudioStreamFrequencyRatio), with a gentle correction based on the queue so it
 *     neither builds up nor starves, like MAME's "syncrefresh";
 *   - other rates (75, 120, 144 Hz...): present on every refresh and emulate the frames that
 *     are due according to the clock (accumulator), at the board's exact rate.
 *
 * Video: the Neo Geo draws 320x224 pixels that the monitor stretches to 4:3, so its pixels are
 * not square. The "sharp" filter first scales up by an integer factor without smoothing and
 * then fits to the final size with linear interpolation: even pixels without blurry edges.
 */
#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats.h"
#include "config.h"
#include "debug.h"
#include "dispatcher.h"
#include "frontend.h"
#include "font.h"
#include "lang.h"
#include "logo.h"
#include "menu.h"
#include "neogeo.h"
#include "profile.h"
#include "port.h"
#include "sounds.h"

void machine_start(const Options *o);

Config cfg;
static char pref_dir[1024], cfg_path[1100];
static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *tex, *tex_big;
static int big_scale;
static SDL_AudioStream *audio;
static SDL_Gamepad *pads[2];
static int test_frames;                      /* frames left to hold the test button */
static char notice[256];
static Uint64 notice_until;
static int quit;
static int step;                             /* F6 in debug mode: one frame */
static unsigned auto_phase[2];
static int play_frame = -1;                  /* "Play": frames since requested (-1 = no) */

void fe_notify(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(notice, sizeof notice, fmt, ap);
    va_end(ap);
    notice_until = SDL_GetTicksNS() + 2500000000ULL;
}

/* ------------------------------------------------------------------ SRAM */
static void sram_path(char *r, size_t n) { snprintf(r, n, "%ssram.bin", pref_dir); }

static void sram_load(void)
{
    char r[1100];
    sram_path(r, sizeof r);
    FILE *f = fopen(r, "rb");
    if (f) {
        if (fread(ng.sram, 1, sizeof ng.sram, f) != sizeof ng.sram) memset(ng.sram, 0, sizeof ng.sram);
        fclose(f);
    }
}

static void sram_save(void)
{
    char r[1100];
    sram_path(r, sizeof r);
    FILE *f = fopen(r, "wb");
    if (f) { fwrite(ng.sram, 1, sizeof ng.sram, f); fclose(f); }
}

/* ------------------------------------------------------------------ save states */
static void state_path(char *r, size_t n)
{
    snprintf(r, n, "%sstate_%d.bin", pref_dir, cfg.slot);
    char old[1100];                       /* older versions: estado_N.bin, carried over */
    snprintf(old, sizeof old, "%sestado_%d.bin", pref_dir, cfg.slot);
    if (!SDL_GetPathInfo(r, NULL) && SDL_GetPathInfo(old, NULL)) SDL_RenamePath(old, r);
}

void fe_state_save(void)
{
    size_t size;
    uint8_t *d = ng_state_save(&size);
    char r[1100];
    state_path(r, sizeof r);
    FILE *f = d ? fopen(r, "wb") : NULL;
    int ok = f && fwrite(d, 1, size, f) == size;
    if (f && fclose(f) != 0) ok = 0;
    free(d);
    fe_notify(T(ok ? "State saved to slot %d" : "Could not save slot %d"), cfg.slot);
}

void fe_state_load(void)
{
    char r[1100];
    state_path(r, sizeof r);
    size_t size;
    void *d = SDL_LoadFile(r, &size);
    if (!d) { fe_notify(T("Slot %d is empty"), cfg.slot); return; }
    if (ng_state_load(d, size) == 0) fe_notify(T("State loaded from slot %d"), cfg.slot);
    else fe_notify(T("Slot %d is from another port version"), cfg.slot);
    SDL_free(d);
}

/* ------------------------------------------------------------------ inputs */
static void read_inputs(void)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    for (int j = 0; j < 2; j++) {
        uint8_t joy = 0, sys = 0;
        static const uint8_t JOY_BIT[8] = {NG_UP, NG_DOWN, NG_LEFT, NG_RIGHT, NG_A, NG_B, NG_C, NG_D};
        int pressed[ACT_COUNT] = {0};
        for (int a = 0; a < ACT_COUNT; a++) {
            if (cfg.key[j][a] != SDL_SCANCODE_UNKNOWN && k[cfg.key[j][a]]) pressed[a] = 1;
            if (pads[j] && cfg.button[j][a] != SDL_GAMEPAD_BUTTON_INVALID &&
                SDL_GetGamepadButton(pads[j], cfg.button[j][a])) pressed[a] = 1;
        }
        if (pads[j]) {                         /* the left stick always moves */
            int ax = SDL_GetGamepadAxis(pads[j], SDL_GAMEPAD_AXIS_LEFTX);
            int ay = SDL_GetGamepadAxis(pads[j], SDL_GAMEPAD_AXIS_LEFTY);
            if (ay < -16000) pressed[ACT_UP] = 1;
            if (ay > 16000) pressed[ACT_DOWN] = 1;
            if (ax < -16000) pressed[ACT_LEFT] = 1;
            if (ax > 16000) pressed[ACT_RIGHT] = 1;
        }
        /* Autofire: pressed for half of the period. */
        int mode = cfg.autofire[j];
        int auto_on = (mode == AUTO_BUTTON && pressed[ACT_AUTO]) || (mode == AUTO_FIRE && pressed[ACT_A]);
        if (auto_on) {
            unsigned period = (unsigned)AUTO_PERIOD[cfg.auto_speed];
            int press = auto_phase[j]++ % period < period / 2;
            if (mode == AUTO_FIRE) pressed[ACT_A] = press;
            else if (press) pressed[ACT_A] = 1;
        } else {
            auto_phase[j] = 0;
        }
        for (int a = 0; a < 8; a++) if (pressed[a]) joy |= JOY_BIT[a];
        if (pressed[ACT_START]) sys |= NG_START;
        if (pressed[ACT_COIN]) sys |= NG_COIN;
        /* A direction and its opposite at the same time cannot happen on a real joystick. */
        if ((joy & (NG_UP | NG_DOWN)) == (NG_UP | NG_DOWN)) joy &= ~(NG_UP | NG_DOWN);
        if ((joy & (NG_LEFT | NG_RIGHT)) == (NG_LEFT | NG_RIGHT)) joy &= ~(NG_LEFT | NG_RIGHT);
        ng.joy[j] = joy;
        ng.system[j] = sys;
    }
    cheats_input(&ng.joy[0], &ng.system[0]);   /* developer menu: SELECT + D for the player */
    if (play_frame >= 0) {                   /* "Play": coin and, a moment later, START */
        if (play_frame < 6) ng.system[0] |= NG_COIN;
        else if (play_frame >= 50 && play_frame < 56) ng.system[0] |= NG_START;
        if (++play_frame >= 56) play_frame = -1;
    }
    ng.test = (k[SDL_SCANCODE_F2] || test_frames > 0) ? 0x00 : 0x80;
    if (test_frames > 0) test_frames--;
}

static void pad_added(SDL_JoystickID id)
{
    for (int j = 0; j < 2; j++)
        if (!pads[j]) {
            pads[j] = SDL_OpenGamepad(id);
            if (pads[j]) fe_notify(T("Gamepad %d: %s"), j + 1, SDL_GetGamepadName(pads[j]));
            return;
        }
}

static void pad_removed(SDL_JoystickID id)
{
    for (int j = 0; j < 2; j++)
        if (pads[j] && SDL_GetGamepadID(pads[j]) == id) {
            SDL_CloseGamepad(pads[j]);
            pads[j] = NULL;
            fe_notify(T("Gamepad %d disconnected"), j + 1);
        }
}

/* ------------------------------------------------------------------ menu actions */
void fe_save_config(void) { config_save(&cfg, cfg_path); }
static int no_wait;                          /* tests: run as fast as possible */

void fe_apply_video(void)
{
    SDL_SetWindowFullscreen(window, cfg.fullscreen != 0);
    SDL_SetRenderVSync(render, cfg.vsync && !no_wait ? 1 : 0);
}

/* Refresh rate of the window's monitor (Hz; 60 if unknown). */
static float monitor_rate(void)
{
    if (getenv("PD_HZ")) return (float)atof(getenv("PD_HZ"));      /* tests: another rate */
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    return m && m->refresh_rate > 1 ? m->refresh_rate : 60.0f;
}
void fe_apply_volume(void) { if (audio) SDL_SetAudioStreamGain(audio, cfg.volume / 100.0f); }
void fe_play(void) { play_frame = 0; }
void fe_service(void) { test_frames = 30; }

/* Red blood: the game reads its BLOOD option as on (profile.h). */
void fe_apply_blood(void)
{
    ng_forced_read[NG_FORCED_BLOOD] = (NgForcedRead){cfg.blood, PROFILE_BLOOD_WORD, PROFILE_BLOOD_MASK, 0x0001};
}
/* Developer menu (cheats.h): only with developer mode on and during a mission. */
void fe_dev_menu(void)
{
    if (!cfg.devmode) fe_notify(T("Turn on developer mode first (Extras > Debug)"));
    else if (!cheats_in_game(0)) fe_notify(T("Only during a mission"));
    else cheats_dev_menu();
}

void fe_quit(void) { quit = 1; }

void fe_reset(void)
{
    uint8_t sram[sizeof ng.sram];
    memcpy(sram, ng.sram, sizeof sram);
    ng_power_on();
    memcpy(ng.sram, sram, sizeof sram);
    play_frame = -1;
    fe_notify(T("Machine reset"));
}

/* ------------------------------------------------------------------ drawing */
static SDL_FRect target_rect(int w, int h)
{
    double ar = cfg.aspect == ASPECT_4_3 ? 4.0 / 3.0 : (double)NG_WIDTH / NG_HEIGHT;
    double dw = w, dh = w / ar;
    if (dh > h) { dh = h; dw = h * ar; }
    if (cfg.filter == FILTER_PIXEL && cfg.aspect == ASPECT_SQUARE) {
        int s = (int)(dh / NG_HEIGHT);         /* exact integer scale if it fits */
        if (s >= 1) { dw = NG_WIDTH * s; dh = NG_HEIGHT * s; }
    }
    SDL_FRect r = {(float)((w - dw) / 2), (float)((h - dh) / 2), (float)dw, (float)dh};
    r.x = SDL_roundf(r.x); r.y = SDL_roundf(r.y);
    return r;
}

static SDL_FRect draw_game(int w, int h)
{
    SDL_UpdateTexture(tex, NULL, ng.screen, NG_WIDTH * 4);
    SDL_FRect d = target_rect(w, h);
    if (cfg.filter == FILTER_SHARP) {
        int s = (int)SDL_ceil(SDL_max(d.w / NG_WIDTH, d.h / NG_HEIGHT));
        s = s < 1 ? 1 : s > 8 ? 8 : s;
        if (s != big_scale) {
            if (tex_big) SDL_DestroyTexture(tex_big);
            tex_big = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                        NG_WIDTH * s, NG_HEIGHT * s);
            SDL_SetTextureScaleMode(tex_big, SDL_SCALEMODE_LINEAR);
            big_scale = s;
        }
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_SetRenderTarget(render, tex_big);
        SDL_RenderTexture(render, tex, NULL, NULL);
        SDL_SetRenderTarget(render, NULL);
        SDL_RenderTexture(render, tex_big, NULL, &d);
    } else {
        SDL_SetTextureScaleMode(tex, cfg.filter == FILTER_SMOOTH ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(render, tex, NULL, &d);
    }
    if (cfg.scanlines && d.h >= NG_HEIGHT * 2) {
        static SDL_FRect lines[NG_HEIGHT];
        float pitch = d.h / NG_HEIGHT;
        for (int y = 0; y < NG_HEIGHT; y++)
            lines[y] = (SDL_FRect){d.x, d.y + y * pitch + pitch * 0.55f, d.w, pitch * 0.45f};
        SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 110);
        SDL_RenderFillRects(render, lines, NG_HEIGHT);
    }
    return d;
}

static void draw_notices(int w, int h)
{
    if (!notice[0] || SDL_GetTicksNS() >= notice_until) return;
    float k = font_factor(w, h);
    SDL_SetRenderScale(render, k, k);
    SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(render, 0, 0, 0, 170);
    SDL_FRect f = {4, 4, font_width(notice, 1) + 8, 14};              /* top left */
    SDL_RenderFillRect(render, &f);
    font_text(render, 8, 7, 1, INK_SILVER, notice);
    SDL_SetRenderScale(render, 1, 1);
}

/* ------------------------------------------------------------------ events */
/* Navigation shared by the menus: 0 up, 1 down, 2 left, 3 right, 4 select, 5 back, 6 stop,
   7 rename, 8 rumble (the last two only in the sound menu). */
static int nav_key(SDL_Scancode sc)
{
    switch (sc) {
    case SDL_SCANCODE_UP: return 0;
    case SDL_SCANCODE_DOWN: return 1;
    case SDL_SCANCODE_LEFT: return 2;
    case SDL_SCANCODE_RIGHT: return 3;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: case SDL_SCANCODE_KP_ENTER: return 4;
    case SDL_SCANCODE_ESCAPE: return 5;
    case SDL_SCANCODE_DELETE: case SDL_SCANCODE_BACKSPACE: return 6;
    case SDL_SCANCODE_F2: case SDL_SCANCODE_R: return 7;
    case SDL_SCANCODE_V: return 8;
    default: return -1;
    }
}

static int nav_button(SDL_GamepadButton b)
{
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 0;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 1;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 2;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 3;
    case SDL_GAMEPAD_BUTTON_SOUTH: return 4;
    case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_GUIDE: return 5;
    case SDL_GAMEPAD_BUTTON_NORTH: return 6;
    case SDL_GAMEPAD_BUTTON_WEST: return 8;
    default: return -1;
    }
}

static void on_key(const SDL_KeyboardEvent *e, int *paused, int *screenshots)
{
    SDL_Scancode sc = e->scancode;
    if (menu_capturing()) {
        if (!e->repeat) menu_capture_key(sc);
        return;
    }
    if (sounds_editing()) { sounds_edit_key(sc); return; }
    if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (e->mod & SDL_KMOD_ALT))) {
        cfg.fullscreen = !cfg.fullscreen;
        fe_apply_video();
        return;
    }
    int nav = nav_key(sc);
    if (sounds_open()) { if (nav >= 0) sounds_navigate(nav); return; }
    if (menu_open()) { if (nav >= 0 && nav <= 5) menu_navigate(nav); return; }
    if (e->repeat) return;
    int dbg = debug_key(e);
    if (dbg == DEBUG_STEP) { *paused = 1; step = 1; }
    if (dbg != DEBUG_NONE) { cfg.debug = debug_enabled(); return; }
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: menu_pause(); break;
    case SDL_SCANCODE_P: *paused = !*paused; fe_notify(T(*paused ? "Paused" : "Resume")); break;
    case SDL_SCANCODE_F4: fe_dev_menu(); break;
    case SDL_SCANCODE_F5: fe_state_save(); break;
    case SDL_SCANCODE_F9: fe_state_load(); break;
    case SDL_SCANCODE_F12: {
        char r[1100];
        snprintf(r, sizeof r, "%sscreenshot_%03d.bmp", pref_dir, (*screenshots)++);
        if (save_bmp(r) == 0) fe_notify(T("Screenshot: %s"), r);
        break;
    }
    default: break;
    }
}

static void on_button(const SDL_GamepadButtonEvent *e)
{
    SDL_GamepadButton b = (SDL_GamepadButton)e->button;
    if (menu_capturing()) { menu_capture_button(b); return; }
    if (sounds_editing()) return;
    SDL_Gamepad *g = SDL_GetGamepadFromID(e->which);
    int combo = g && ((b == SDL_GAMEPAD_BUTTON_START && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) ||
                      (b == SDL_GAMEPAD_BUTTON_BACK && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START)));
    int nav = nav_button(b);
    if (sounds_open()) { if (nav >= 0) sounds_navigate(nav); return; }
    if (menu_open()) {
        if (combo) menu_navigate(5);
        else if (nav >= 0 && nav <= 5) menu_navigate(nav);
        return;
    }
    if (b == SDL_GAMEPAD_BUTTON_GUIDE || combo) menu_pause();
}

/* ------------------------------------------------------------------ ROMs */
static volatile int dialog_done;
static char dialog_path[1024];

static void SDLCALL on_folder_chosen(void *data, const char *const *list, int filter)
{
    (void)data; (void)filter;
    if (list && list[0]) snprintf(dialog_path, sizeof dialog_path, "%s", list[0]);
    dialog_done = 1;
}

static int try_roms(const char *folder, char *error, int size)
{
    if (ng_roms_load(&ng_roms, folder, error, size) == 0) {
        snprintf(cfg.roms, sizeof cfg.roms, "%s", folder);
        return 1;
    }
    char sub[1100];                          /* the game's root folder works too */
    snprintf(sub, sizeof sub, "%s\\resources\\game", folder);
    if (ng_roms_load(&ng_roms, sub, error, size) == 0) {
        snprintf(cfg.roms, sizeof cfg.roms, "%s", sub);
        return 1;
    }
    return 0;
}

/* --roms, the remembered folder, "roms" next to the executable, the GOG installation and, if
   none of those works, the folder chosen by the user. */
static int find_roms(const Options *o)
{
    char error[1200] = "", folder[1024];
    if (o->roms) {
        if (try_roms(o->roms, error, sizeof error)) return 1;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Metal Slug X", error, NULL);
        return 0;
    }
    if (cfg.roms[0] && try_roms(cfg.roms, error, sizeof error)) return 1;
    const char *base = SDL_GetBasePath();
    snprintf(folder, sizeof folder, "%sroms", base ? base : "");
    if (try_roms(folder, error, sizeof error)) return 1;
    if (gog_find(folder, sizeof folder) && try_roms(folder, error, sizeof error)) return 1;
    for (;;) {
        char msg[1600];
        snprintf(msg, sizeof msg,
                 T("No valid GOG copy of Metal Slug X was found.\n\n%s\n\n"
                   "Choose the folder where you installed the game (or its resources\\game subfolder)."), error);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Metal Slug X", msg, NULL);
        dialog_done = 0;
        dialog_path[0] = 0;
        SDL_ShowOpenFolderDialog(on_folder_chosen, NULL, NULL, NULL, false);
        while (!dialog_done) {
            SDL_Event e;
            SDL_WaitEventTimeout(&e, 50);
        }
        if (!dialog_path[0]) return 0;           /* cancelled */
        if (try_roms(dialog_path, error, sizeof error)) return 1;
    }
}

/* ------------------------------------------------------------------ main */
int window_run(const Options *o)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | (o->no_audio ? 0 : SDL_INIT_AUDIO))) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    /* PD_PREF: another preferences folder (tests, without touching the user's configuration). */
    char *pref = getenv("PD_PREF") ? NULL : SDL_GetPrefPath("ProjectoDecompile", "mslugx");
    snprintf(pref_dir, sizeof pref_dir, "%s", pref ? pref : getenv("PD_PREF") ? getenv("PD_PREF") : "");
    SDL_free(pref);
    snprintf(cfg_path, sizeof cfg_path, "%smslugx.ini", pref_dir);
    config_defaults(&cfg);
    config_load(&cfg, cfg_path);
    lang_set(cfg.language);
    if (o->scale > 0) cfg.scale = o->scale;
    if (o->turbo >= 0) cfg.turbo = o->turbo;

    if (!find_roms(o)) { SDL_Quit(); return 1; }
    config_save(&cfg, cfg_path);

    ng_audio_enabled = !o->no_audio;
    cpu_turbo = cfg.turbo;
    machine_start(o);
    debug_enable(cfg.debug);
    fe_apply_blood();
    cheats_devmode = cfg.devmode;
    cheats_apply();
    rumble_init(pads, 2);
    rumble_strength(cfg.rumble);
    if (!o->empty_sram) sram_load();

    int height = NG_HEIGHT * cfg.scale;
    int width = cfg.aspect == ASPECT_4_3 ? height * 4 / 3 : NG_WIDTH * cfg.scale;
    if (!SDL_CreateWindowAndRenderer("Metal Slug X", width, height, SDL_WINDOW_RESIZABLE, &window, &render)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, NG_WIDTH, NG_HEIGHT);
    tex = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, NG_WIDTH, NG_HEIGHT);
    fe_apply_video();
    font_create(render);
    {
        char r[1100];
        snprintf(r, sizeof r, "%stitle_logo.png", pref_dir);
        logo_prepare(render, r);              /* the first time, runs the machine up to the title */
    }
    {
        char r[1100], old[1100];
        snprintf(r, sizeof r, "%ssounds.ini", pref_dir);
        snprintf(old, sizeof old, "%ssonidos.ini", pref_dir);
        /* Older versions named the file sonidos.ini: carry it over to the new name. */
        if (!SDL_GetPathInfo(r, NULL) && SDL_GetPathInfo(old, NULL)) SDL_RenamePath(old, r);
        sounds_init(window, r);
    }
    if (cfg.start_screen) menu_start();

    /* Audio: SDL converts from 55 555 Hz to the device rate. */
    if (ng_audio_enabled) {
        const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, NG_AUDIO_HZ};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (audio) {
            /* Initial 50 ms cushion of silence against jitter in the video pacing. */
            static const int16_t silence[NG_AUDIO_HZ / 20 * 2];
            SDL_PutAudioStreamData(audio, silence, (int)sizeof silence);
            SDL_SetAudioStreamGain(audio, cfg.volume / 100.0f);
            SDL_ResumeAudioStreamDevice(audio);
        }
        else { fprintf(stderr, "No audio: %s\n", SDL_GetError()); ng_audio_enabled = 0; }
    }
    static int16_t samples[4096 * 2];
    uint64_t audio_frames = 0, audio_drops = 0, audio_empty = 0, audio_queue_sum = 0;
    if (!cfg.start_screen) fe_notify(T("Esc: menu   F5/F9: save/load game"));

    /* PD_WINDOW_TEST=N,file.png[,menu|settings|cheats|debug|sounds|play|rename]: after N frames,
       saves what the window shows (with the pause menu, its Settings page or the Extras pages, with the sound menu playing the
       second music track, or after choosing "Play" at frame 1300) and quits. */
    long test_frame_count = 0;
    char test_png[1024] = "";
    int test_menu = 0, test_settings = 0, test_extras = -1, test_sounds = 0, test_play = 0, test_rename = 0;
    if (getenv("PD_WINDOW_TEST")) {
        char extra[16] = "";
        sscanf(getenv("PD_WINDOW_TEST"), "%ld,%1023[^,],%15s", &test_frame_count, test_png, extra);
        test_settings = !strcmp(extra, "settings");
        if (!strcmp(extra, "cheats")) test_extras = 0;
        if (!strcmp(extra, "debug")) test_extras = 1;
        test_menu = test_settings || test_extras >= 0 || !strcmp(extra, "menu");
        test_sounds = !strcmp(extra, "sounds");
        test_play = !strcmp(extra, "play");
        test_rename = !strcmp(extra, "rename");
    }
    long frames_shown = 0;
    if (test_frame_count) { no_wait = 1; fe_apply_video(); }

    int paused = 0, screenshots = 0;
    Uint64 next = SDL_GetTicksNS(), previous = next, accumulated = 0, start = next;
    float hz = monitor_rate();
    /* PD_QUIT_AFTER=seconds: quits on its own (real-time pacing measurement, with --trace). */
    double quit_after = getenv("PD_QUIT_AFTER") ? atof(getenv("PD_QUIT_AFTER")) : 0;
    uint64_t loops = 0, emulated = ng.frames;
    while (!quit) {
        if (test_frame_count && test_menu && frames_shown == test_frame_count - 1) {
            menu_pause();
            if (test_settings) {                     /* Continue, Save, Load, Settings */
                for (int i = 0; i < 3; i++) menu_navigate(1);
                menu_navigate(4);
            }
            if (test_extras >= 0) {                  /* Extras, then Cheats or Debug */
                for (int i = 0; i < 5; i++) menu_navigate(1);
                menu_navigate(4);
                for (int i = 0; i < test_extras; i++) menu_navigate(1);
                menu_navigate(4);
            }
        }
        if (test_frame_count && test_play && frames_shown == 1300) menu_navigate(4);   /* "Play" */
        if (test_frame_count && test_rename) {              /* rename the same effect twice */
            long f = test_frame_count - frames_shown;
            if (f == 400) { sounds_show(); sounds_navigate(3); sounds_navigate(7); }
            if (f == 300 || f == 100) {
                SDL_Delay(80);                              /* past the window that ignores the R key */
                if (f == 100) for (int i = 0; i < 40; i++) sounds_edit_key(SDL_SCANCODE_BACKSPACE);
                sounds_text(f == 300 ? "Test one" : "Test two");
                sounds_edit_key(SDL_SCANCODE_RETURN);
            }
            if (f == 200) sounds_navigate(7);
        }
        if (test_frame_count && test_sounds && frames_shown == test_frame_count - 60) {
            sounds_show();
            sounds_navigate(1);
            sounds_navigate(4);
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: quit = 1; break;
            case SDL_EVENT_KEY_DOWN: on_key(&e.key, &paused, &screenshots); break;
            case SDL_EVENT_TEXT_INPUT: sounds_text(e.text.text); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: on_button(&e.gbutton); break;
            case SDL_EVENT_GAMEPAD_ADDED: pad_added(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_REMOVED: pad_removed(e.gdevice.which); break;
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: cfg.fullscreen = 1; hz = monitor_rate(); break;
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: cfg.fullscreen = 0; hz = monitor_rate(); break;
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED: hz = monitor_rate(); break;
            default: break;
            }
        }
        /* How many frames to emulate in this iteration (see the comment at the top). */
        Uint64 now = SDL_GetTicksNS();
        int vsync = cfg.vsync && !no_wait;
        int exact = vsync && hz >= 58.0f && hz <= 62.0f;
        int frames = 1;
        if (vsync && !exact) {
            accumulated += now - previous;
            if (accumulated > 4 * NS_PER_FRAME) accumulated = NS_PER_FRAME;  /* we fell behind */
            frames = (int)(accumulated / NS_PER_FRAME);
            accumulated -= (Uint64)frames * NS_PER_FRAME;
        }
        previous = now;
        loops++;
        if (quit_after > 0 && (now - start) / 1e9 >= quit_after) quit = 1;
        for (int c = 0; c < frames; c++) {
            if (sounds_open()) {
                sounds_frame();
            } else if (menu_game_runs()) {     /* start screen: the demo keeps running, no inputs */
                memset(ng.joy, 0, sizeof ng.joy);
                memset(ng.system, 0, sizeof ng.system);
                ng.test = 0x80;
                ng_run_frame();
            } else if ((!paused || step) && !menu_open()) {
                read_inputs();
                ng_run_frame();
                debug_frame();
                rumble_frame();
                step = 0;
            }
        }
        if (audio && vsync) {
            /* With V-sync the monitor is the clock: audio is played at the ratio of the frame
               rate, with a correction of up to +-0.5 % to keep the queue at ~50 ms. */
            float base = exact ? hz / 59.1856f : 1.0f;
            float goal = NG_AUDIO_HZ * 4 / 20.0f;
            float error = ((float)SDL_GetAudioStreamQueued(audio) - goal) / goal;
            if (error > 1) error = 1;
            if (error < -1) error = -1;
            SDL_SetAudioStreamFrequencyRatio(audio, base * (1.0f + 0.005f * error));
        } else if (audio) {
            SDL_SetAudioStreamFrequencyRatio(audio, 1.0f);
        }
        if (audio) {
            uint32_t n = ng_audio_read(samples, 4096);
            /* If the device falls behind (more than ~150 ms queued), the audio is dropped so
               latency does not build up; the video sets the pace at 59.1856 Hz. */
            int queue = SDL_GetAudioStreamQueued(audio);
            audio_frames++;
            audio_queue_sum += (uint64_t)(queue > 0 ? queue : 0);
            if (queue == 0) audio_empty++;
            if (queue < NG_AUDIO_HZ * 4 / 100) {           /* < 10 ms: pad so it does not cut out */
                static const int16_t padding[NG_AUDIO_HZ / 50 * 2];
                SDL_PutAudioStreamData(audio, padding, (int)sizeof padding);
            }
            if (n && queue < NG_AUDIO_HZ * 4 * 15 / 100)
                SDL_PutAudioStreamData(audio, samples, (int)(n * 4));
            else if (n)
                audio_drops++;
        }
        int w, h;
        SDL_GetRenderOutputSize(render, &w, &h);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
        SDL_RenderClear(render);
        SDL_FRect game = draw_game(w, h);
        debug_draw(render, game, w, h, cfg.turbo);
        if (sounds_open()) sounds_draw(render, w, h);
        else menu_draw(render, w, h);
        draw_notices(w, h);
        if (test_frame_count && ++frames_shown >= test_frame_count) {
            SDL_Surface *s = SDL_RenderReadPixels(render, NULL);
            if (s) { SDL_SavePNG(s, test_png); SDL_DestroySurface(s); }
            quit = 1;
        }
        SDL_RenderPresent(render);

        /* Without V-sync, waiting sets the pace: one frame every 16.896 ms. */
        next += NS_PER_FRAME;
        now = SDL_GetTicksNS();
        if (no_wait || vsync) next = now;                   /* tests, or V-sync already waited */
        else if (next > now) SDL_DelayPrecise(next - now);
        else if (now - next > 4 * NS_PER_FRAME) next = now;   /* we fell behind: do not accumulate */
    }

    sounds_close();                    /* SRAM and state go back to being the game's */
    if (!o->empty_sram) sram_save();
    config_save(&cfg, cfg_path);
    if (ng_trace) {
        double s = (SDL_GetTicksNS() - start) / 1e9;
        fprintf(ng_trace, "pacing: monitor %.2f Hz, vsync %d, %.2f loops/s, %.3f emulated frames/s\n", hz, cfg.vsync,
                loops / s, (ng.frames - emulated) / s);
    }
    if (audio && ng_trace && audio_frames)
        fprintf(ng_trace, "audio: %llu frames, mean queue %.1f ms, queue empty %llu times, drops %llu\n",
                (unsigned long long)audio_frames, audio_queue_sum / (double)audio_frames / (NG_AUDIO_HZ * 4) * 1000,
                (unsigned long long)audio_empty, (unsigned long long)audio_drops);
    if (audio) SDL_DestroyAudioStream(audio);
    for (int j = 0; j < 2; j++) if (pads[j]) SDL_CloseGamepad(pads[j]);
    font_destroy();
    logo_destroy();
    if (tex_big) SDL_DestroyTexture(tex_big);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(window);
    SDL_Quit();
    ng_roms_free(&ng_roms);
    return 0;
}
