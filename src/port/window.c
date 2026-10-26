/* Windowed mode (Phase 5): video, audio, 2-player controls, menu, save states.
 * Phase 6C: start screen and menus drawn with the game's font (menu.c, font.c).
 *
 * Shortcuts (configurable, config.h HK_*; these are the defaults): Esc = pause menu, P = pause,
 * F5 / F9 = save / load state in the active slot, F7 / F8 = previous / next slot, Tab = fast
 * forward, F11 = fullscreen (Alt+Enter too, always), F12 = screenshot, F2 = test button (BIOS
 * service menu: difficulty, lives...), F3 = debug panel (debug.c), F4 = developer menu. On the
 * gamepad: Guide opens the menu (Back + Start always does), the right trigger fast-forwards and
 * Share / Capture takes a screenshot; the triggers count as buttons.
 *
 * Autofire: while held, button A is pressed and released automatically with a period of
 * 6, 8 or 12 frames. Measured in game (pistol sound command F7): the game does not fire more
 * than 10 times per second even if the pattern is faster, so 6 frames is the useful maximum.
 * The rest of the controls are configured in the menu or in <game id>.ini (config.c).
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
#ifdef _WIN32
#define COBJMACROS
#include <d3d11.h>
#include <dxgi.h>
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cheats.h"
#include "config.h"
#include "debug.h"
#include "dispatcher.h"
#include "frontend.h"
#include "progress.h"
#include "records.h"
#include "bossbar.h"
#include "bossrush.h"
#include "font.h"
#include "ui_font.h"
#include "lang.h"
#include "logo.h"
#include "menu.h"
#include "neogeo.h"
#include "profile.h"
#include "port.h"
#include "sounds.h"
#include "crt.h"
#include "runahead.h"
#include "version.h"

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
/* Presses seen as events since the last frame: a key pressed and released between two frames
   still counts for one frame (the keyboard and gamepad states are only read once per frame). */
static uint8_t latched[2][ACT_COUNT];
static int mission_game;                      /* the game was started from Mission mode */
static int play_frame = -1;                  /* "Play": frames since requested (-1 = no) */
/* Boss Rush from saved states (Metal Slug, bossrush.h): its states being made (the screen shows
   the progress instead of the game), and when that started. */
static int rush_prep;
static Uint64 rush_prep_since;
static uint64_t power_on_frame;               /* ng.frames at the last reset */
#define READY_FRAMES 900                      /* from power-on to the attract mode (title ~680) */
static uint64_t play_ready = READY_FRAMES;    /* "Play" waits this long after power-on */
/* Fast boot: up to this frame the BIOS boot runs unseen, with no inputs and silent, as fast as
   possible (the same frames as always: the machine's state afterwards does not change). */
static uint64_t boot_until;
static void boot_start(void)
{
    boot_until = cfg.fast_boot ? power_on_frame + game_profile.boot_frames : 0;
}
static int booting(void) { return ng.frames < boot_until; }

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

int fe_arcade_active(void);

void fe_state_save(void)
{
    if (fe_arcade_active()) { fe_notify(T("Not in Arcade mode")); return; }
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
    if (fe_arcade_active()) { fe_notify(T("Not in Arcade mode")); return; }
    char r[1100];
    state_path(r, sizeof r);
    size_t size;
    void *d = SDL_LoadFile(r, &size);
    if (!d) { fe_notify(T("Slot %d is empty"), cfg.slot); return; }
    if (ng_state_load(d, size) == 0) {
        boot_until = 0;
        records_invalidate();
        bossbar_reset();
        fe_notify(T("State loaded from slot %d"), cfg.slot);
    }
    else fe_notify(T("Slot %d is from another port version"), cfg.slot);
    SDL_free(d);
}

/* ------------------------------------------------------------------ inputs */
SDL_Gamepad *fe_gamepad(int p) { return pads[p & 1] ? pads[p & 1] : pads[!(p & 1)]; }

/* A gamepad input (PAD_*) held: a button, or a trigger or a stick direction past half way. */
static int pad_down(SDL_Gamepad *g, int in)
{
    if (!g || in == PAD_NONE) return 0;
    if (in >= PAD_LS_UP && in <= PAD_RS_RIGHT) {
        int k = in - PAD_LS_UP;                     /* up, down, left, right; left stick, right stick */
        static const SDL_GamepadAxis AX[4] = {SDL_GAMEPAD_AXIS_LEFTY, SDL_GAMEPAD_AXIS_LEFTX,
                                               SDL_GAMEPAD_AXIS_RIGHTY, SDL_GAMEPAD_AXIS_RIGHTX};
        int v = SDL_GetGamepadAxis(g, AX[k / 2]);
        return k & 1 ? v > 16000 : v < -16000;
    }
    if (in == PAD_LT) return SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 16000;
    if (in == PAD_RT) return SDL_GetGamepadAxis(g, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 16000;
    return SDL_GetGamepadButton(g, (SDL_GamepadButton)in);
}

/* A shortcut held on the keyboard or on either gamepad. */
static int hk_held(int h)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    if (cfg.hk_key[h] != SDL_SCANCODE_UNKNOWN && k[cfg.hk_key[h]]) return 1;
    return pad_down(pads[0], cfg.hk_pad[h]) || pad_down(pads[1], cfg.hk_pad[h]);
}

static void arcade_coins(void);
static void read_inputs(void)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    for (int j = 0; j < 2; j++) {
        uint8_t joy = 0, sys = 0;
        static const uint8_t JOY_BIT[8] = {NG_UP, NG_DOWN, NG_LEFT, NG_RIGHT, NG_A, NG_B, NG_C, NG_D};
        int pressed[ACT_COUNT] = {0};
        for (int a = 0; a < ACT_COUNT; a++) {
            if (cfg.key[j][a] != SDL_SCANCODE_UNKNOWN && k[cfg.key[j][a]]) pressed[a] = 1;
            if (pad_down(pads[j], cfg.button[j][a])) pressed[a] = 1;
            if (latched[j][a]) pressed[a] = 1;
            latched[j][a] = 0;
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
    arcade_coins();
    /* "Play": coin and, a moment later, START; after a reset, once the game is up. */
    if (play_frame >= 0 && ng.frames - power_on_frame >= play_ready) {
        if (play_frame < 6) ng.system[0] |= NG_COIN;
        else if (play_frame >= 50 && play_frame < 56) ng.system[0] |= NG_START;
        if (++play_frame >= 56) play_frame = -1;
    }
    ng.test = (hk_held(HK_SERVICE) || test_frames > 0) ? 0x00 : 0x80;
    if (test_frames > 0) test_frames--;
}

static void guid_of(SDL_JoystickID id, char *b, int n)
{
    SDL_GUIDToString(SDL_GetGamepadGUIDForID(id), b, n);
}

/* A gamepad now player p's: the profile linked to it, if any, becomes theirs. */
static void pad_profile(int p, const char *guid)
{
    int k = config_profile_of(&cfg, guid);
    if (k < 0 || k == cfg.pad_profile[p]) return;
    cfg.pad_profile[p] = k;
    config_profile_load(&cfg, p);
}

/* A gamepad connected: the player it is remembered for (cfg.pad_guid), else the first player
   without one. A player's remembered gamepad takes their slot back from another one. */
static void pad_added(SDL_JoystickID id)
{
    for (int j = 0; j < 2; j++)
        if (pads[j] && SDL_GetGamepadID(pads[j]) == id) return;
    char guid[40];
    guid_of(id, guid, sizeof guid);
    int slot = -1;
    for (int j = 0; j < 2 && slot < 0; j++)
        if (cfg.pad_guid[j][0] && !strcmp(cfg.pad_guid[j], guid)) {
            if (pads[j]) {
                char other[40];
                guid_of(SDL_GetGamepadID(pads[j]), other, sizeof other);
                if (!strcmp(other, guid)) continue;            /* the same model already there */
                if (!pads[!j]) pads[!j] = pads[j];             /* the one there moves to the free slot */
                else SDL_CloseGamepad(pads[j]);
                pads[j] = NULL;
            }
            slot = j;
        }
    for (int j = 0; j < 2 && slot < 0; j++)
        if (!pads[j]) slot = j;
    if (slot < 0) return;
    pads[slot] = SDL_OpenGamepad(id);
    if (pads[slot]) {
        fe_notify(T("Gamepad %d: %s"), slot + 1, SDL_GetGamepadName(pads[slot]));
        pad_profile(slot, guid);
    }
}

SDL_Gamepad *fe_player_gamepad(int p) { return pads[p & 1]; }

void fe_cycle_gamepad(int p, int d)
{
    int n = 0;
    SDL_JoystickID *ids = SDL_GetGamepads(&n);
    if (!ids || n == 0) { SDL_free(ids); fe_notify(T("No gamepad connected")); return; }
    int cur = -1;
    for (int i = 0; i < n; i++)
        if (pads[p] && SDL_GetGamepadID(pads[p]) == ids[i]) cur = i;
    SDL_JoystickID id = ids[cur < 0 ? (d > 0 ? 0 : n - 1) : ((cur + d) % n + n) % n];
    SDL_free(ids);
    if (pads[p] && SDL_GetGamepadID(pads[p]) == id) return;
    SDL_Gamepad *g = NULL;
    if (pads[!p] && SDL_GetGamepadID(pads[!p]) == id) {        /* the other player's: they swap */
        g = pads[!p];
        pads[!p] = pads[p];
        if (pads[!p]) guid_of(SDL_GetGamepadID(pads[!p]), cfg.pad_guid[!p], sizeof cfg.pad_guid[!p]);
    } else {
        if (pads[p]) SDL_CloseGamepad(pads[p]);
        g = SDL_OpenGamepad(id);
    }
    pads[p] = g;
    if (g) {
        guid_of(id, cfg.pad_guid[p], sizeof cfg.pad_guid[p]);
        fe_notify(T("Gamepad %d: %s"), p + 1, SDL_GetGamepadName(g));
        pad_profile(p, cfg.pad_guid[p]);
    }
    if (pads[!p]) pad_profile(!p, cfg.pad_guid[!p]);
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
/* The ear hears loudness logarithmically: a linear gain left 10 % at only -20 dB (still loud).
   The cube of the percentage gives -60 dB at 10 %, -31 dB at 30 %, -18 dB at 50 %. */
static float volume_gain(void)
{
    float v = cfg.volume / 100.0f;
    return v * v * v;
}
void fe_apply_volume(void) { if (audio) SDL_SetAudioStreamGain(audio, volume_gain()); }
void fe_play(void)
{
    play_frame = 0;
    play_ready = READY_FRAMES;
    mission_game = 0;
    cheats_start_mission_once = -1;               /* a Mission mode start left pending */
    cheats_mission_from = 0;
}
void fe_service(void) { test_frames = 30; }

/* Red blood: the game reads its BLOOD option as on (profile.h). */
void fe_apply_blood(void)
{
    ng_forced_read[NG_FORCED_BLOOD] = (NgForcedRead){cfg.blood && profile_supported(), game_profile.blood_word,
                                                     game_profile.blood_mask, (uint16_t)(game_profile.blood_mask & 0x0101), 0, 0};
}
/* Developer menu (cheats.h): only with developer mode on and during a mission. */
void fe_dev_menu(void)
{
    if (!profile_supported()) fe_notify(T("Unrecognized game version: cheats and debug are off"));
    else if (!cfg.devmode) fe_notify(T("Turn on developer mode first (Extras > Debug)"));
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
    power_on_frame = ng.frames;
    play_frame = -1;
    cheats_mission_from = 0;
    boot_start();
    records_reset_tracking();
    bossbar_reset();
    fe_notify(T("Machine reset"));
}

/* ------------------------------------------------------------------ continue where you left off */
/* A game in progress (a player playing or in the continue countdown; not Arcade mode) is saved to
   resume.bin: every minute of play, when a mission is unlocked, when going back to the start screen
   and on quit. The start screen then offers "Continue". Window tests write it only with PD_PREF. */
static int resume_writes = 1;
static uint64_t resume_next;                    /* ng.frames of the next autosave */
#define RESUME_EVERY 3551                       /* about one minute (59.1856 frames per second) */
static void resume_path(char *r, size_t n) { snprintf(r, n, "%sresume.bin", pref_dir); }

int fe_resume_available(void)
{
    char r[1100];
    resume_path(r, sizeof r);
    SDL_PathInfo info;
    return SDL_GetPathInfo(r, &info) && info.size > 0;
}

int fe_resume(void)
{
    char r[1100];
    resume_path(r, sizeof r);
    size_t size;
    void *d = SDL_LoadFile(r, &size);
    int ok = d && ng_state_load(d, size) == 0;
    SDL_free(d);
    if (ok) { boot_until = 0; records_invalidate(); bossbar_reset(); }
    if (!ok) fe_notify(T("Could not continue the last game"));
    return ok;
}

uint32_t fe_score_p1(void)
{
    uint32_t score = 0;
    if (game_profile.score_p1 && profile_supported())
        for (int i = 0; i < 4; i++) {
            uint8_t b = ng.ram[(game_profile.score_p1 + (uint32_t)i) & 0xFFFF];
            score = score * 100 + (uint32_t)(b >> 4) * 10 + (b & 15);
        }
    return score;
}

int fe_mission_now(void)
{
    if (!game_profile.mission_now || !profile_supported()) return 0;
    int m = ng.ram[game_profile.mission_now & 0xFFFF] + 1;
    return m >= 1 && m <= (int)game_profile.missions ? m : 0;
}

/* Next to resume.bin, for the start screen's "Continue" panel: resume.png (the screen when the
   game was left) and resume.ini (mission, score, when). */
static SDL_Texture *resume_thumb;
static int resume_thumb_tried;

static void resume_save(void)
{
    if (resume_thumb) { SDL_DestroyTexture(resume_thumb); resume_thumb = NULL; }
    resume_thumb_tried = 0;                       /* the "Continue" panel shows the new one */
    resume_next = ng.frames + RESUME_EVERY;
    size_t size;
    uint8_t *d = ng_state_save(&size);
    char r[1100];
    resume_path(r, sizeof r);
    FILE *f = d ? fopen(r, "wb") : NULL;
    if (f) { fwrite(d, 1, size, f); fclose(f); }
    free(d);
    snprintf(r, sizeof r, "%sresume.png", pref_dir);
    SDL_Surface *s = SDL_CreateSurfaceFrom(NG_WIDTH, NG_HEIGHT, SDL_PIXELFORMAT_XRGB8888, ng.screen, NG_WIDTH * 4);
    if (s) { SDL_SavePNG(s, r); SDL_DestroySurface(s); }
    snprintf(r, sizeof r, "%sresume.ini", pref_dir);
    SDL_Time now = 0;
    SDL_GetCurrentTime(&now);
    if ((f = fopen(r, "w"))) {
        fprintf(f, "mission=%d\nscore=%u\nsaved=%lld\n", fe_mission_now(), (unsigned)fe_score_p1(), (long long)now);
        fclose(f);
    }
}

int fe_resume_info(FeResumeInfo *i)
{
    memset(i, 0, sizeof *i);
    char r[1100];
    snprintf(r, sizeof r, "%sresume.ini", pref_dir);
    FILE *f = fopen(r, "r");
    if (!f) return 0;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        long long v;
        if (sscanf(line, "mission=%lld", &v) == 1) i->mission = (int)v;
        else if (sscanf(line, "score=%lld", &v) == 1) i->score = (uint32_t)v;
        else if (sscanf(line, "saved=%lld", &v) == 1) i->saved = v;
    }
    fclose(f);
    return 1;
}

SDL_Texture *fe_resume_thumbnail(void)
{
    if (!resume_thumb_tried) {
        resume_thumb_tried = 1;
        char r[1100];
        snprintf(r, sizeof r, "%sresume.png", pref_dir);
        SDL_Surface *s = SDL_LoadPNG(r);
        if (s) {
            resume_thumb = SDL_CreateTextureFromSurface(render, s);
            SDL_DestroySurface(s);
            if (resume_thumb) SDL_SetTextureScaleMode(resume_thumb, SDL_SCALEMODE_LINEAR);
        }
    }
    return resume_thumb;
}

/* ------------------------------------------------------------------ Arcade mode */
/* A whole game like the machine: no saving or loading, no cheats or debug options, the
   original speed and no fast-forward; continues limited to the chosen number (each coin
   inserted after the first one is a continue). It ends at the game over (BIOS player status
   3: 1 playing, 2 continue countdown) with the score and the time. */
static struct {
    int active, started, left, used, over;
    uint64_t frames;
    uint16_t dips;
    int devmode, start_mission, turbo, debug;
    uint8_t coin_was, blocking;
    int rush, cleared;                        /* Boss Rush */
    int slowdown_off;                         /* Boss Rush with the slowdown off: no record */
    int weapon_in;                            /* Boss Rush: frames in game until the weapon item (-1: given) */
} arc;

int fe_arcade_active(void) { return arc.active; }

/* A game worth continuing later: not Arcade mode, not the start screen's demo, a player in game. */
static int resume_worth(void)
{
    uint8_t s1 = ng.ram[0xFDB6], s2 = ng.ram[0xFDB7];  /* BIOS player status: 1 playing, 2 countdown */
    int mission_known = fe_mission_now() || !game_profile.mission_now || !profile_supported();
    return resume_writes && !arc.active && !menu_game_runs() && (s1 == 1 || s1 == 2 || s2 == 1 || s2 == 2) &&
           mission_known;                                   /* not while a mission loads */
}

void fe_back_to_start(void)
{
    if (resume_worth()) resume_save();
    fe_arcade_stop();
    fe_reset();
}

void fe_arcade_start(int continues)
{
    memset(&arc, 0, sizeof arc);
    arc.active = 1;
    mission_game = 0;
    arc.left = continues;                     /* -1: infinite */
    arc.dips = cheats_dips; arc.devmode = cheats_devmode; arc.start_mission = cheats_start_mission;
    arc.turbo = cpu_turbo; arc.debug = debug_enabled();
    cheats_dips = 0; cheats_devmode = 0; cheats_start_mission = 0; cheats_start_mission_once = -1;
    cheats_apply();
    cpu_turbo = 0;
    debug_enable(0);
    play_frame = 0;                           /* the first coin and START */
    play_ready = READY_FRAMES;
    fe_notify(T("Arcade mode: good luck!"));
}

void fe_rush_start(void)
{
    /* saved states (Metal Slug): made first if missing, then the run starts (main loop) */
    if (bossrush_needs_prepare()) {
        rush_prep = 1;
        rush_prep_since = SDL_GetTicksNS();
        bossrush_prepare_begin();
        return;
    }
    fe_arcade_start(0);
    arc.rush = 1;
    arc.weapon_in = game_profile.rush_weapon ? 120 : -1;
    cpu_turbo = cfg.turbo;                    /* Boss Rush keeps the Slowdown option */
    if (!bossrush_start()) {
        fe_arcade_stop();
        fe_notify(T("Boss Rush could not start"));
        return;
    }
    if (game_profile.n_rush_states) play_frame = -1;     /* the state is in game: no coin or START */
    fe_notify(T("Boss Rush: good luck!"));
}

/* The preparation cancelled (Esc, Back): the states made so far are kept; the machine (left
   wherever the fast-forward was) restarts. */
static void rush_prep_cancel(void)
{
    bossrush_prepare_cancel();
    rush_prep = 0;
    fe_reset();
    menu_start();
    fe_notify(T("Boss Rush: preparation stopped (the bosses ready are kept)"));
}

/* The preparation's screen: what is happening, the boss, a bar and the time so far. */
static void rush_prep_draw(SDL_Renderer *r, int w, int h)
{
    float s = (float)h / 224.0f;
    SDL_Color yellow = {0xFF, 0xBC, 0x3D, 255}, white = {0xF2, 0xF2, 0xF2, 255}, grey = {0xA0, 0xA0, 0xA8, 255};
    float cx = w / 2.0f, y = h * 0.30f;
    ui_text_centered(r, UI_STENCIL, 16 * s, 0, cx, y, yellow, T("PREPARING BOSS RUSH"));
    ui_text_centered(r, UI_REGULAR, 7 * s, 0, cx, y + 18 * s, white,
                     T("First time only: the game is played to each boss and saved there"));
    int i = bossrush_prepare_boss(), n = bossrush_total();
    if (i >= n) i = n - 1;
    const ProfileBoss *b = &game_profile.bosses[game_profile.rush_states[i].boss];
    char line[160];
    snprintf(line, sizeof line, T("Boss %d of %d: %s"), i + 1, n, b->name ? b->name : T("BOSS"));
    ui_text_centered(r, UI_SEMIBOLD, 8 * s, 0, cx, y + 40 * s, white, line);
    float bw = 200 * s, bh = 6 * s, bx = cx - bw / 2, by = y + 52 * s;
    SDL_FRect back = {bx, by, bw, bh}, done = {bx, by, bw * bossrush_prepare_progress(), bh};
    SDL_SetRenderDrawColor(r, 0x30, 0x30, 0x38, 255);
    SDL_RenderFillRect(r, &back);
    SDL_SetRenderDrawColor(r, yellow.r, yellow.g, yellow.b, 255);
    SDL_RenderFillRect(r, &done);
    Uint64 secs = (SDL_GetTicksNS() - rush_prep_since) / 1000000000ULL;
    snprintf(line, sizeof line, T("%d:%02d so far (a few minutes in all)"), (int)(secs / 60), (int)(secs % 60));
    ui_text_centered(r, UI_REGULAR, 7 * s, 0, cx, by + 16 * s, grey, line);
    ui_text_centered(r, UI_REGULAR, 7 * s, 0, cx, by + 30 * s, grey, T("Esc / Back: stop (the bosses ready are kept)"));
}

int fe_rush_mode(void) { return arc.rush; }

void fe_rush_result(int *defeated, int *total, int *cleared)
{
    *defeated = bossrush_defeated();
    *total = bossrush_total();
    *cleared = arc.cleared;
}

void fe_arcade_stop(void)
{
    if (!arc.active) return;
    arc.active = 0;
    if (arc.rush) { bossrush_stop(); arc.turbo = cfg.turbo; }
    cheats_dips = arc.dips; cheats_devmode = arc.devmode; cheats_start_mission = arc.start_mission;
    cheats_apply();
    cpu_turbo = arc.turbo;
    debug_enable(arc.debug);
}

void fe_arcade_result(uint32_t *score, uint64_t *frames, int *continues)
{
    *score = fe_score_p1();
    *frames = arc.frames;
    *continues = arc.used;
}

/* Arcade mode: a coin is a continue; without continues left the coin slot is closed. */
static void arcade_coins(void)
{
    if (!arc.active) return;
    uint8_t coin = (uint8_t)((ng.system[0] | ng.system[1]) & NG_COIN);
    if (coin && !arc.coin_was) {                   /* a new press */
        arc.blocking = arc.left == 0;
        if (arc.blocking) fe_notify(T("No continues left"));
        else {
            if (arc.left > 0) arc.left--;
            arc.used++;
        }
    }
    arc.coin_was = coin;
    if (arc.blocking) {
        if (!coin) arc.blocking = 0;
        ng.system[0] &= (uint8_t)~NG_COIN;
        ng.system[1] &= (uint8_t)~NG_COIN;
    }
}

/* Each frame in Arcade mode: coins (continues) and the end of the game. */
static void arcade_frame(void)
{
    uint8_t s1 = ng.ram[0xFDB6], s2 = ng.ram[0xFDB7];
    int playing = s1 == 1 || s1 == 2 || s2 == 1 || s2 == 2;
    if (s1 == 1 || s2 == 1) arc.started = 1;
    if (arc.started && playing && !arc.over) arc.frames++;
    if (arc.rush && cpu_turbo) arc.slowdown_off = 1;
    /* Boss Rush: 2 s after the player is in the first boss section, the weapon's item (the Heavy
       Machine Gun) appears in front of them, as the stages' items: picking it up is their choice */
    if (arc.rush && arc.weapon_in >= 0 && bossrush_running() && cheats_in_game(0) && --arc.weapon_in < 0)
        cheats_give_weapon(0, game_profile.rush_weapon);
    if (arc.rush && !arc.over) {
        int r = bossrush_frame();
        if (r == BR_NEXT) fe_notify(T("Next boss: %d/%d"), bossrush_defeated() + 1, bossrush_total());
        if (r == BR_CLEARED) {                        /* the last boss fell: the ending follows */
            arc.over = arc.cleared = 1;
            if (arc.slowdown_off) fe_notify(T("Not recorded: slowdown off"));
            else if (records_rush(bossrush_defeated(), (uint32_t)arc.frames, 1)) fe_notify(T("New record!"));
            menu_arcade_results();
        }
    }
    if (arc.started && !playing && !arc.over) {
        arc.over = 1;
        if (arc.rush) {
            if (arc.slowdown_off) fe_notify(T("Not recorded: slowdown off"));
            else if (records_rush(bossrush_defeated(), (uint32_t)arc.frames, 0)) fe_notify(T("New record!"));
            bossrush_stop();
        } else {
            int mission = game_profile.mission_now ? ng.ram[game_profile.mission_now & 0xFFFF] + 1 : 0;
            if (mission > (int)game_profile.missions) mission = 0;
            if (records_arcade(fe_score_p1(), mission)) fe_notify(T("New record!"));
        }
        menu_arcade_results();
    }
}

/* Mission mode: a new game that starts at mission m (0-based). With the game's PLAY MANUAL known
   (profile.h), straight to the soldier select: a reset, the coin and START as soon as the boot is
   over, no "How to play" (cheats.h, cheats_mission_from). */
void fe_mission_start(int m)
{
    int direct = game_profile.manual_word && profile_supported();
    if (direct) fe_reset();
    mission_game = 1;
    cheats_start_mission_once = m;
    cheats_start_section_once = -1;
    cheats_mission_from = direct ? power_on_frame + game_profile.boot_frames : 0;
    play_ready = direct ? game_profile.boot_frames : READY_FRAMES;
    play_frame = 0;
    if (direct) fe_notify(T("Mission %d"), m + 1);
}

/* Window size: s x 224 lines (the aspect's width). */
void fe_window_size(int s)
{
    if (cfg.fullscreen) return;
    int height = NG_HEIGHT * s;
    int width = cfg.aspect == ASPECT_4_3 ? height * 4 / 3 : NG_WIDTH * s;
    SDL_SetWindowSize(window, width, height);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
}

/* The largest window scale that fits the window's monitor. */
int fe_max_window_scale(void)
{
    SDL_Rect r;
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &r)) return 16;
    int s = r.h / NG_HEIGHT;
    while (s > 1 && (cfg.aspect == ASPECT_4_3 ? NG_HEIGHT * s * 4 / 3 : NG_WIDTH * s) > r.w) s--;
    return s < 1 ? 1 : s > 16 ? 16 : s;
}

/* ------------------------------------------------------------------ drawing */
static SDL_FRect target_rect(int w, int h)
{
    double ar = cfg.aspect == ASPECT_4_3 ? 4.0 / 3.0 : (double)NG_WIDTH / NG_HEIGHT;
    double dw = w, dh = w / ar;
    if (dh > h) { dh = h; dw = h * ar; }
    if (cfg.integer_scale || (cfg.filter == FILTER_PIXEL && cfg.aspect == ASPECT_SQUARE)) {
        int s = (int)(dh / NG_HEIGHT);         /* a whole multiple of 224 lines, if it fits */
        if (s >= 1) { dh = NG_HEIGHT * s; dw = cfg.aspect == ASPECT_4_3 ? SDL_round(dh * 4 / 3) : NG_WIDTH * s; }
    }
    SDL_FRect r = {(float)((w - dw) / 2), (float)((h - dh) / 2), (float)dw, (float)dh};
    r.x = SDL_roundf(r.x); r.y = SDL_roundf(r.y);
    return r;
}

static SDL_FRect draw_game(int w, int h)
{
    SDL_UpdateTexture(tex, NULL, ng.screen, NG_WIDTH * 4);
    if (game_profile.black_bottom_corners) {           /* the game's side bars, down to the last row */
        static uint32_t black[16 * 16];
        int n = game_profile.black_bottom_corners > 16 ? 16 : game_profile.black_bottom_corners;
        for (int i = 0; i < n * n; i++) black[i] = 0xFF000000u;
        SDL_Rect left = {0, NG_HEIGHT - n, n, n}, right = {NG_WIDTH - n, NG_HEIGHT - n, n, n};
        SDL_UpdateTexture(tex, &left, black, n * 4);
        SDL_UpdateTexture(tex, &right, black, n * 4);
    }
    SDL_FRect d = target_rect(w, h);
    SDL_Texture *base = tex;
    if (cfg.filter == FILTER_SHARP || cfg.crt) {             /* the CRT effect starts from the sharp image */
        int s = (int)SDL_ceil(SDL_max(d.w / NG_WIDTH, d.h / NG_HEIGHT));
        s = s < 1 ? 1 : s > 24 ? 24 : s;               /* up to 8K (4320 lines: 19.3) */
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
        if (!cfg.crt) SDL_RenderTexture(render, tex_big, NULL, &d);   /* the CRT effect draws it itself */
        base = tex_big;
    } else {
        SDL_SetTextureScaleMode(tex, cfg.filter == FILTER_SMOOTH ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(render, tex, NULL, &d);
    }
    if (cfg.crt) crt_draw(render, base, d, cfg.crt, cfg.crt_curve);
    else if (cfg.scanlines && d.h >= NG_HEIGHT * 2) {
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
    /* the menus' canvas: 720 units tall, at least 960 wide; top right */
    float k = h / 720.0f;
    if (w / k < 960) k = w / 960.0f;
    SDL_SetRenderScale(render, k, k);
    SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
    float tw = ui_text_width(UI_SEMIBOLD, 16, 0.5f, notice), right = w / k - 24;
    SDL_FRect f = {right - tw - 36, 20, tw + 36, 40};
    SDL_SetRenderDrawColor(render, 0x0A, 0x0C, 0x0D, 235);
    SDL_RenderFillRect(render, &f);
    SDL_SetRenderDrawColor(render, 0xF2, 0xA9, 0x00, 255);
    SDL_FRect line = {f.x, f.y + f.h - 2, f.w, 2};
    SDL_RenderFillRect(render, &line);
    ui_text(render, UI_SEMIBOLD, 16, 0.5f, f.x + 18, f.y + 19, (SDL_Color){0xEC, 0xE6, 0xD8, 255}, notice);
    SDL_SetRenderScale(render, 1, 1);
}

/* ------------------------------------------------------------------ events */
/* Navigation shared by the menus: 0 up, 1 down, 2 left, 3 right, 4 select, 5 back, 6 stop,
   7 rename, 8 rumble (the last two only in the sound menu), 9 / 10 previous / next tab. */
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
    case SDL_SCANCODE_Q: case SDL_SCANCODE_PAGEUP: return 9;
    case SDL_SCANCODE_E: case SDL_SCANCODE_PAGEDOWN: return 10;
    default: return -1;
    }
}

static int nav_button(SDL_GamepadButton b)
{
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 0;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 1;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 2;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 3;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 9;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 10;
    case SDL_GAMEPAD_BUTTON_SOUTH: return 4;
    case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_GUIDE: return 5;
    case SDL_GAMEPAD_BUTTON_NORTH: return 6;
    case SDL_GAMEPAD_BUTTON_WEST: return 8;
    default: return -1;
    }
}

static void latch_key(SDL_Scancode sc)
{
    for (int j = 0; j < 2; j++)
        for (int a = 0; a < ACT_COUNT; a++)
            if (cfg.key[j][a] == sc) latched[j][a] = 1;
}

static void latch_pad(SDL_JoystickID which, int in)
{
    for (int j = 0; j < 2; j++)
        if (pads[j] && SDL_GetGamepadID(pads[j]) == which)
            for (int a = 0; a < ACT_COUNT; a++)
                if (cfg.button[j][a] == in) latched[j][a] = 1;
}

/* The shortcut bound to a key (-1 if none). */
static int hk_of_key(SDL_Scancode sc)
{
    for (int h = 0; h < HK_COUNT; h++)
        if (sc != SDL_SCANCODE_UNKNOWN && cfg.hk_key[h] == sc) return h;
    return -1;
}

static int hk_of_pad(int in)
{
    for (int h = 0; h < HK_COUNT; h++)
        if (in != PAD_NONE && cfg.hk_pad[h] == in) return h;
    return -1;
}

static void screenshot(void);

static void change_slot(int d)
{
    cfg.slot = (cfg.slot - 1 + d + 9) % 9 + 1;
    fe_notify(T("Slot %d"), cfg.slot);
}

/* A shortcut pressed: those that work anywhere (screenshot, fullscreen) and, during the game,
   the rest. Fast forward and the service button act while held (main loop, read_inputs).
   Returns 1 if it was handled. */
static int hotkey(int h, int *paused)
{
    switch (h) {
    case HK_SCREENSHOT: screenshot(); return 1;
    case HK_FULLSCREEN: cfg.fullscreen = !cfg.fullscreen; fe_apply_video(); return 1;
    default: break;
    }
    if (menu_open() || sounds_open()) return 0;
    switch (h) {
    case HK_MENU: menu_pause(); return 1;
    case HK_PAUSE: *paused = !*paused; fe_notify(T(*paused ? "Paused" : "Resume")); return 1;
    case HK_SAVE: fe_state_save(); return 1;
    case HK_LOAD: fe_state_load(); return 1;
    case HK_SLOT_PREV: change_slot(-1); return 1;
    case HK_SLOT_NEXT: change_slot(1); return 1;
    case HK_DEBUG: debug_enable(!debug_enabled()); cfg.debug = debug_enabled(); return 1;
    case HK_DEV_MENU: fe_dev_menu(); return 1;
    default: return 0;
    }
}

/* With V-sync, the Direct3D 11 driver lets up to 3 frames queue before showing them (DXGI's
   default), about 50 ms between a press and its image: at most 1 frame is allowed instead. */
static void reduce_frame_latency(SDL_Renderer *r)
{
    const char *name = SDL_GetRendererName(r);
    if (ng_trace) fprintf(ng_trace, "renderer: %s\n", name ? name : "?");
#ifdef _WIN32
    if (!name || strcmp(name, "direct3d11")) return;
    ID3D11Device *dev = SDL_GetPointerProperty(SDL_GetRendererProperties(r), SDL_PROP_RENDERER_D3D11_DEVICE_POINTER, NULL);
    IDXGIDevice1 *dxgi = NULL;
    if (dev && SUCCEEDED(ID3D11Device_QueryInterface(dev, &IID_IDXGIDevice1, (void **)&dxgi))) {
        IDXGIDevice1_SetMaximumFrameLatency(dxgi, 1);
        IDXGIDevice1_Release(dxgi);
    }
#endif
}

/* F12 or the gamepad's Share / Capture button: the game image (320 x 224, without the menus or
   the bar) as a PNG in the "screenshots" folder of the preferences, named by date and time. */
static void screenshot(void)
{
    char dir[1100], r[1200];
    snprintf(dir, sizeof dir, "%sscreenshots", pref_dir);
    SDL_CreateDirectory(dir);
    SDL_Time now = 0;
    SDL_DateTime t = {0};
    SDL_GetCurrentTime(&now);
    SDL_TimeToDateTime(now, &t, true);
    SDL_PathInfo info;
    for (int n = 1; n < 100; n++) {                /* two in the same second: _2, _3... */
        int len = snprintf(r, sizeof r, "%s/%s_%04d%02d%02d_%02d%02d%02d", dir, ng_game->id, t.year, t.month,
                           t.day, t.hour, t.minute, t.second);
        if (n > 1) snprintf(r + len, sizeof r - (size_t)len, "_%d", n);
        snprintf(r + strlen(r), sizeof r - strlen(r), ".png");
        if (!SDL_GetPathInfo(r, &info)) break;
    }
    SDL_Surface *s = SDL_CreateSurfaceFrom(NG_WIDTH, NG_HEIGHT, SDL_PIXELFORMAT_XRGB8888, ng.screen, NG_WIDTH * 4);
    int ok = s && SDL_SavePNG(s, r);
    if (s) SDL_DestroySurface(s);
    if (ok) fe_notify(T("Screenshot saved"));
    else fe_notify(T("Could not save the screenshot"));
}

/* The gamepad was used last: the mouse cursor is hidden and the mouse does not move the menus'
   selection until it really moves or a key is pressed (cfg.pad_hides_cursor). */
static int pad_mode;

static void on_key(const SDL_KeyboardEvent *e, int *paused)
{
    pad_mode = 0;
    SDL_Scancode sc = e->scancode;
    if (!e->repeat && !menu_capturing() && !sounds_editing()) latch_key(sc);
    if (menu_capturing()) {
        if (!e->repeat) menu_capture_key(sc);
        return;
    }
    if (sounds_editing()) { sounds_edit_key(sc); return; }
    int h = hk_of_key(sc);
    if (sc == SDL_SCANCODE_RETURN && (e->mod & SDL_KMOD_ALT)) h = HK_FULLSCREEN;   /* always */
    if ((h == HK_SCREENSHOT || h == HK_FULLSCREEN) && !e->repeat) { hotkey(h, paused); return; }
    int nav = nav_key(sc);
    menu_set_gamepad(0);
    if (rush_prep) { if (!e->repeat && (nav == 5 || h == HK_MENU)) rush_prep_cancel(); return; }
    if (sounds_open()) { if (nav >= 0) sounds_navigate(nav); return; }
    if (menu_open()) { if ((nav >= 0 && nav <= 6) || nav >= 9) menu_navigate(nav); return; }
    if (e->repeat) return;
    if (h == HK_DEBUG) { hotkey(h, paused); return; }
    int dbg = debug_key(e);
    if (dbg == DEBUG_STEP) { *paused = 1; step = 1; }
    if (dbg != DEBUG_NONE) { cfg.debug = debug_enabled(); return; }
    if (h >= 0) hotkey(h, paused);
}

/* A gamepad input pressed: a button, or a trigger going past half way (PAD_LT / PAD_RT). */
static void on_pad(SDL_JoystickID which, int in, int *paused)
{
    if (cfg.pad_hides_cursor) pad_mode = 1;
    if (menu_capturing()) { menu_capture_button(in); return; }
    if (sounds_editing()) return;
    int h = hk_of_pad(in);
    if (h == HK_SCREENSHOT || h == HK_FULLSCREEN) { hotkey(h, paused); return; }
    SDL_Gamepad *g = SDL_GetGamepadFromID(which);
    int combo = g && ((in == SDL_GAMEPAD_BUTTON_START && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) ||
                      (in == SDL_GAMEPAD_BUTTON_BACK && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START)));
    int nav = in >= 0 && in < SDL_GAMEPAD_BUTTON_COUNT ? nav_button((SDL_GamepadButton)in)
            : in >= PAD_LS_UP && in <= PAD_LS_RIGHT ? in - PAD_LS_UP : -1;     /* the left stick (a lever) too */
    menu_set_gamepad(1);
    if (rush_prep) { if (nav == 5 || combo) rush_prep_cancel(); return; }
    if (sounds_open()) { if (nav >= 0) sounds_navigate(nav); return; }
    if (menu_open()) {
        if (combo) menu_navigate(5);
        else if ((nav >= 0 && nav <= 6) || nav >= 9) menu_navigate(nav);
        return;
    }
    if (combo) menu_pause();                         /* Back + Start: always the menu */
    else if (h >= 0) hotkey(h, paused);
}

/* The triggers and the sticks' directions as buttons: a press when one goes past half way,
   released under a quarter. */
static void on_axis(const SDL_GamepadAxisEvent *e, int *paused)
{
    static uint8_t down[2][PAD_INPUTS - PAD_LT];
    int in[2] = {PAD_NONE, PAD_NONE};                   /* [negative, positive] */
    switch (e->axis) {
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER: in[1] = PAD_LT; break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER: in[1] = PAD_RT; break;
    case SDL_GAMEPAD_AXIS_LEFTX: in[0] = PAD_LS_LEFT; in[1] = PAD_LS_RIGHT; break;
    case SDL_GAMEPAD_AXIS_LEFTY: in[0] = PAD_LS_UP; in[1] = PAD_LS_DOWN; break;
    case SDL_GAMEPAD_AXIS_RIGHTX: in[0] = PAD_RS_LEFT; in[1] = PAD_RS_RIGHT; break;
    case SDL_GAMEPAD_AXIS_RIGHTY: in[0] = PAD_RS_UP; in[1] = PAD_RS_DOWN; break;
    default: return;
    }
    for (int j = 0; j < 2; j++) {
        if (!pads[j] || SDL_GetGamepadID(pads[j]) != e->which) continue;
        for (int s = 0; s < 2; s++) {
            if (in[s] == PAD_NONE) continue;
            int v = s ? e->value : -(int)e->value;
            uint8_t *d = &down[j][in[s] - PAD_LT];
            if (!*d && v > 16000) {
                *d = 1;
                latch_pad(e->which, in[s]);
                on_pad(e->which, in[s], paused);
            } else if (*d && v < 8000) {
                *d = 0;
            }
        }
    }
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

/* The user's copy: a folder (searched a few levels deep) or a zip; remembered in <game id>.ini. */
static int try_roms(const char *path, char *error, int size)
{
    if (ng_roms_load(&ng_roms, path, error, size) != 0) return 0;
    snprintf(cfg.roms, sizeof cfg.roms, "%s", path);
    return 1;
}

/* "Game not found" screen: drop a folder or zip on the window, or ENTER for a folder picker.
   Drawn with SDL's built-in 8x8 ASCII font (the game's own font comes from the copy). */
static int ask_for_game(char *error, int error_size)
{
    SDL_Window *w;
    SDL_Renderer *r;
    if (!SDL_CreateWindowAndRenderer(ng_game->title, 960, 672, SDL_WINDOW_RESIZABLE, &w, &r)) return 0;
    SDL_SetRenderLogicalPresentation(r, 320, 224, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    int found = 0, done = 0, loading = 0;
    char dropped[1024] = "";
    while (!done) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) done = 1;
            else if (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_ESCAPE) done = 1;
            else if (e.type == SDL_EVENT_DROP_FILE && e.drop.data) snprintf(dropped, sizeof dropped, "%s", e.drop.data);
            else if ((e.type == SDL_EVENT_KEY_DOWN && (e.key.key == SDLK_RETURN || e.key.key == SDLK_KP_ENTER)) ||
                     (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_LEFT) ||
                     (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH)) {
                dialog_done = 0;
                dialog_path[0] = 0;
                SDL_ShowOpenFolderDialog(on_folder_chosen, NULL, w, NULL, false);
                while (!dialog_done) {
                    SDL_Event ignored;
                    SDL_WaitEventTimeout(&ignored, 50);
                }
                if (dialog_path[0]) snprintf(dropped, sizeof dropped, "%s", dialog_path);
            }
        }
        if (getenv("PD_FIND_TEST")) done = 1;       /* test: draw once and quit */
        SDL_SetRenderDrawColor(r, 16, 16, 24, 255);
        SDL_RenderClear(r);
        static const char *const LINES[] = {
            "Your copy of the game was not found.", "",
            "Drop its folder or zip on this window,", "or press ENTER to choose a folder.", "",
            "It works with the GOG or Steam release", "(the install folder) or a MAME set",
            "(the game's zip + neogeo.zip, or files).", "",
            "Or copy it to the \"game\" folder", "next to the executable.",
        };
        /* Games without a PC release (Metal Slug 5): only the MAME set. */
        static const char *const LINES_MAME[] = {
            "Your copy of the game was not found.", "",
            "Drop its folder or zip on this window,", "or press ENTER to choose a folder.", "",
            "It works with the MAME set", "(the game's zip + neogeo.zip,", "or their files).", "",
            "Or copy it to the \"game\" folder", "next to the executable.",
        };
        const char *const *lines = ng_has_pc_release() ? LINES : LINES_MAME;
        SDL_SetRenderDrawColor(r, 255, 200, 64, 255);
        char heading[96];
        snprintf(heading, sizeof heading, "%s  native port v%s", game_profile.title, PORT_VERSION);
        SDL_RenderDebugText(r, 8, 8, heading);
        SDL_SetRenderDrawColor(r, 230, 230, 230, 255);
        float y = 28;
        for (size_t i = 0; i < sizeof LINES / sizeof LINES[0]; i++, y += 10)
            if (lines[i][0]) SDL_RenderDebugText(r, 8, y, T(lines[i]));
        /* the reason, wrapped at 38 characters */
        SDL_SetRenderDrawColor(r, 255, 120, 100, 255);
        y += 8;
        const char *msg = loading ? T("Loading...") : error;
        for (size_t at = 0, n = strlen(msg); at < n && y < 200; at += 38, y += 10) {
            char line[40];
            snprintf(line, sizeof line, "%.38s", msg + at);
            SDL_RenderDebugText(r, 8, y, line);
        }
        SDL_SetRenderDrawColor(r, 160, 160, 160, 255);
        SDL_RenderDebugText(r, 8, 210, T("ESC: quit"));
        if (getenv("PD_FIND_TEST")) {             /* PD_FIND_TEST=file.png: saves the screen */
            SDL_Surface *s = SDL_RenderReadPixels(r, NULL);
            if (s) { SDL_SavePNG(s, getenv("PD_FIND_TEST")); SDL_DestroySurface(s); }
        }
        SDL_RenderPresent(r);
        if (dropped[0] && !loading) { loading = 1; continue; }   /* show "Loading..." first */
        if (loading) {
            loading = 0;
            if (try_roms(dropped, error, error_size)) { found = 1; done = 1; }
            dropped[0] = 0;
        }
        SDL_Delay(16);
    }
    SDL_DestroyRenderer(r);
    SDL_DestroyWindow(w);
    return found;
}

/* The path given (--roms or dropped on the executable), the remembered one, the usual places
   (find_game.c: "game"/"roms" next to the executable, GOG, Steam) and, if none of those works,
   the "game not found" screen. */
static int find_roms(const Options *o)
{
    char error[1200] = "", first_error[1200] = "";
    const char *base = SDL_GetBasePath();
    /* The "game" folder next to the program also gives the Neo Geo system files a GOG copy lacks
       (Metal Slug 4), wherever the copy is. */
    static char system_path[1100];
    snprintf(system_path, sizeof system_path, "%sgame", base ? base : "");
    ng_roms_system_path = system_path;
    if (o->roms) {
        if (try_roms(o->roms, error, sizeof error)) return 1;
        return ask_for_game(error, sizeof error);
    }
    if (cfg.roms[0] && try_roms(cfg.roms, error, sizeof error)) return 1;
    static char places[16][1024];
    int n = game_places(base ? base : "", places, 16);
    for (int i = 0; i < n; i++) {
        if (try_roms(places[i], error, sizeof error)) return 1;
        if (!first_error[0]) snprintf(first_error, sizeof first_error, "%s", error);
    }
    if (first_error[0]) snprintf(error, sizeof error, "%s", first_error);
    else error[0] = 0;
    return ask_for_game(error, sizeof error);
}

/* ------------------------------------------------------------------ main */
int window_run(const Options *o)
{
#if defined(_WIN32) && defined(PD_HAS_ICON)
    /* A personal build with the user's icon (resource 1, src/CMakeLists.txt): the window uses it. */
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON, "1");
    SDL_SetHint(SDL_HINT_WINDOWS_INTRESOURCE_ICON_SMALL, "1");
#endif
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | (o->no_audio ? 0 : SDL_INIT_AUDIO))) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    /* PD_PREF: another preferences folder (tests, without touching the user's configuration).
       Portable mode: a "save" folder next to the executable holds the settings, the SRAM, the
       saved games and the logo (e.g. on a USB drive), and nothing is written to the user's
       profile. Otherwise the user's application data folder. */
    char portable[1100] = "";
    const char *base = SDL_GetBasePath();
    SDL_PathInfo info;
    if (base) snprintf(portable, sizeof portable, "%ssave", base);
    int is_portable = !getenv("PD_PREF") && portable[0] && SDL_GetPathInfo(portable, &info) &&
                      info.type == SDL_PATHTYPE_DIRECTORY;
    char *pref = getenv("PD_PREF") || is_portable ? NULL : SDL_GetPrefPath("ProjectoDecompile", ng_game->id);
    if (is_portable) snprintf(pref_dir, sizeof pref_dir, "%s/", portable);
    else snprintf(pref_dir, sizeof pref_dir, "%s", pref ? pref : getenv("PD_PREF") ? getenv("PD_PREF") : "");
    SDL_free(pref);
    snprintf(cfg_path, sizeof cfg_path, "%s%s.ini", pref_dir, ng_game->id);
    {
        char rush_dir[1100];
        snprintf(rush_dir, sizeof rush_dir, "%sbossrush/", pref_dir);
        bossrush_set_dir(rush_dir);
    }
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
    boot_start();
    /* PD_LOAD_STATE=file (window checks): a state saved by the headless mode (PD_SAVE_STATE). */
    if (getenv("PD_LOAD_STATE")) {
        size_t size;
        void *d = SDL_LoadFile(getenv("PD_LOAD_STATE"), &size);
        if (d && ng_state_load(d, size) == 0) boot_until = 0;
        SDL_free(d);
    }
    debug_enable(cfg.debug);
    fe_apply_blood();
    cheats_devmode = cfg.devmode;
    cheats_apply();
    rumble_init(pads, 2);
    rumble_strength(cfg.rumble);
    if (!o->empty_sram) sram_load();

    int height = NG_HEIGHT * cfg.scale;
    int width = cfg.aspect == ASPECT_4_3 ? height * 4 / 3 : NG_WIDTH * cfg.scale;
    if (!SDL_CreateWindowAndRenderer(ng_game->title, width, height, SDL_WINDOW_RESIZABLE, &window, &render)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, NG_WIDTH, NG_HEIGHT);
    reduce_frame_latency(render);
    tex = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, NG_WIDTH, NG_HEIGHT);
    fe_apply_video();
    font_create(render);
    {
        char r[1100];
        snprintf(r, sizeof r, "%stitle_logo.png", pref_dir);
        int logo = logo_prepare(render, r);   /* the first time, runs the machine up to the title */
        fprintf(stderr, "Preferences: %s\nTitle logo: %s\n", pref_dir[0] ? pref_dir : "(none)",
                logo == 0 ? "ok" : "not available (the title screen was not found)");
    }
    {
        char r[1100], old[1100];
        snprintf(r, sizeof r, "%ssounds.ini", pref_dir);
        snprintf(old, sizeof old, "%ssonidos.ini", pref_dir);
        /* Older versions named the file sonidos.ini: carry it over to the new name. */
        if (!SDL_GetPathInfo(r, NULL) && SDL_GetPathInfo(old, NULL)) SDL_RenamePath(old, r);
        sounds_init(window, r);
    }
    {
        char r[1100];
        snprintf(r, sizeof r, "%sprogress.ini", pref_dir);
        progress_load(r);
        snprintf(r, sizeof r, "%srecords.ini", pref_dir);
        records_load(r);
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
            SDL_SetAudioStreamGain(audio, volume_gain());
            SDL_ResumeAudioStreamDevice(audio);
        }
        else { fprintf(stderr, "No audio: %s\n", SDL_GetError()); ng_audio_enabled = 0; }
    }
    static int16_t samples[4096 * 2];
    uint64_t audio_frames = 0, audio_drops = 0, audio_empty = 0, audio_queue_sum = 0;
    if (!cfg.start_screen) {
        char k[3][32];
        const SDL_Scancode hk[3] = {cfg.hk_key[HK_MENU], cfg.hk_key[HK_SAVE], cfg.hk_key[HK_LOAD]};
        for (int i = 0; i < 3; i++) snprintf(k[i], sizeof k[i], "%s", hk[i] ? SDL_GetScancodeName(hk[i]) : "-");
        fe_notify(T("%s: menu   %s/%s: save/load game"), k[0], k[1], k[2]);
    }
    if (!profile_supported()) fe_notify(T("Unrecognized game version: cheats and debug are off"));

    /* PD_WINDOW_TEST=N,file.png[,menu|settings|cheats|debug|sounds|play|rename]: after N frames,
       saves what the window shows (with the pause menu, its Settings page or the Extras pages, with the sound menu playing the
       second music track, or after choosing "Play" at frame 1300) and quits. */
    long test_frame_count = 0;
    char test_png[1024] = "";
    int test_menu = 0, test_settings = 0, test_extras = -1, test_sounds = 0, test_play = 0, test_rename = 0;
    int test_start_item = -1, test_results = 0, test_video = 0, test_arcade_run = 0, test_mission = 0;
    int test_mouse = 0, test_bossbar = 0, test_rush_run = 0, test_shot = 0, test_hotkeys = 0;
    int test_tab = 0, test_rows = 0, test_capture = 0;
    if (getenv("PD_WINDOW_TEST")) {
        char extra[16] = "";
        sscanf(getenv("PD_WINDOW_TEST"), "%ld,%1023[^,],%15s", &test_frame_count, test_png, extra);
        test_settings = !strcmp(extra, "settings");
        if (!strcmp(extra, "cheats")) test_extras = 1;
        if (!strcmp(extra, "debug")) test_extras = 2;
        if (!strcmp(extra, "records")) test_extras = 0;
        test_menu = test_settings || test_extras >= 0 || !strcmp(extra, "menu");
        test_sounds = !strcmp(extra, "sounds");
        test_play = !strcmp(extra, "play");
        test_rename = !strcmp(extra, "rename");
        /* the start screen's Game modes page (Arcade selected) and Mission mode page, the
           results page, Settings > Video, Settings > System (tabs) */
        if (!strcmp(extra, "arcade")) test_start_item = 2;
        if (!strcmp(extra, "missions")) test_start_item = 3;
        if (!strcmp(extra, "resume")) test_start_item = 0;
        if (!strcmp(extra, "system")) test_start_item = 4;
        /* the health bar: the followed enemies count as hit (with PD_LOAD_STATE in a boss fight) */
        test_bossbar = !strcmp(extra, "bossbar");
        test_shot = !strcmp(extra, "screenshot");
        test_hotkeys = !strcmp(extra, "hotkeys");    /* the next-slot and quick-save keys, as events */
        /* the mouse: a move and a click on the start screen's "Game modes" (second option
           without a saved game), then a click on the Arcade card's up arrow (continues 3 -> 5) */
        test_mouse = !strcmp(extra, "mouse");
        if (!strncmp(extra, "mission", 7) && extra[7] >= '1' && extra[7] <= '8') test_mission = extra[7] - '0';
        test_results = !strcmp(extra, "results") || !strcmp(extra, "rushresults");
        if (!strcmp(extra, "rushresults")) arc.rush = arc.cleared = 1;      /* the Boss Rush summary's layout */
        /* Arcade mode with no continues from frame 1300, no inputs: the image when the results
           page opens (or at the frame limit) */
        test_arcade_run = !strcmp(extra, "arcaderun");
        /* Boss Rush from the Game modes page, no inputs (the image at the frame limit or when the
           results page opens) */
        test_rush_run = !strcmp(extra, "rushrun");
        test_video = !strcmp(extra, "video");
        if (test_video) test_menu = test_settings = 1;
        /* Settings > Keyboard / Gamepad (the third row, "Down", selected); kbcapture / padcapture: that
           row waiting for its new key or button; kbscroll: the list scrolled to the shortcuts */
        if (!strcmp(extra, "keyboard") || !strcmp(extra, "kbcapture")) test_tab = 3;
        if (!strcmp(extra, "gamepad") || !strcmp(extra, "padcapture")) test_tab = 4;
        if (!strcmp(extra, "kbscroll")) { test_tab = 3; test_rows = 19; }
        if (!strcmp(extra, "gametab")) { test_tab = 2; test_rows = 3; }    /* Settings > Game, Run-ahead */
        test_capture = !strcmp(extra, "kbcapture") || !strcmp(extra, "padcapture");
        /* kbswap: on the keyboard tab, "Down" gets Z (A, fire): the two swap (the ini says) */
        if (!strcmp(extra, "kbswap")) test_tab = 3, test_capture = 2;
        /* padstick: on the gamepad tab, "Up" gets the left stick pushed up (a lever: the first
           gamepad connected must be there) */
        if (!strcmp(extra, "padstick")) test_tab = 4, test_rows = 5, test_capture = 3;
        /* padprofile: the gamepad tab's Profile row, one step right (profile 2, linked to the gamepad) */
        if (!strcmp(extra, "padprofile")) test_tab = 4, test_rows = 2, test_capture = 4;
        if (test_tab) { test_menu = test_settings = 1; if (!test_rows) test_rows = test_tab == 4 ? 7 : 3; }
    }
    long frames_shown = 0;
    if (test_frame_count) { no_wait = 1; fe_apply_video(); }
    resume_writes = !test_frame_count || getenv("PD_PREF");
    resume_next = ng.frames + RESUME_EVERY;

    int paused = 0, unfocused = 0, cursor_shown = 1;
    Uint64 mouse_seen = SDL_GetTicksNS();
    Uint64 next = SDL_GetTicksNS(), previous = next, accumulated = 0, start = next;
    float hz = monitor_rate();
    /* PD_QUIT_AFTER=seconds: quits on its own (real-time pacing measurement, with --trace). */
    double quit_after = getenv("PD_QUIT_AFTER") ? atof(getenv("PD_QUIT_AFTER")) : 0;
    uint64_t loops = 0, emulated = ng.frames;
    while (!quit) {
        if (test_frame_count && test_menu && frames_shown == test_frame_count - 1) {
            menu_pause();
            if (test_settings) {                     /* Continue, Saves, Settings (its Video tab) */
                for (int i = 0; i < 2; i++) menu_navigate(1);
                menu_navigate(4);
                for (int i = 0; i < test_tab; i++) menu_navigate(10);
                for (int i = 0; i < test_rows; i++) menu_navigate(1);
                if (test_capture == 4) menu_navigate(3);
                else if (test_capture) menu_navigate(4);
                if (test_capture == 2) {
                    SDL_Event k = {0};
                    k.type = SDL_EVENT_KEY_DOWN;
                    k.key.down = true;
                    k.key.scancode = SDL_SCANCODE_Z;
                    SDL_PushEvent(&k);
                }
                if (test_capture == 3 && pads[0]) {
                    SDL_Event a = {0};
                    a.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
                    a.gaxis.which = SDL_GetGamepadID(pads[0]);
                    a.gaxis.axis = SDL_GAMEPAD_AXIS_LEFTY;
                    a.gaxis.value = -30000;
                    SDL_PushEvent(&a);
                }
            }
            if (test_extras >= 0) {                  /* Extras (Music and sounds, Cheats, Debug...) */
                for (int i = 0; i < 3; i++) menu_navigate(1);
                menu_navigate(4);
                /* Extras: Music and sounds, Records (when supported), Cheats, Debug */
                int downs = test_extras + 1 - (test_extras > 0 && !records_supported());
                for (int i = 0; i < downs; i++) menu_navigate(1);
                menu_navigate(4);
            }
        }
        if (test_frame_count && test_play && frames_shown == 1300) menu_navigate(4);   /* "Play" */
        if (test_frame_count && test_start_item >= 0 && frames_shown == test_frame_count - 1) {
            /* start screen: Continue (when there is a saved game), Play, Game modes, Load, Settings */
            int item = test_start_item == 3 ? 2 : test_start_item;
            for (int i = fe_resume_available() ? 0 : 1; i < item; i++) menu_navigate(1);
            menu_navigate(4);
            if (test_start_item == 3) { menu_navigate(3); menu_navigate(4); }    /* the Mission card */
            if (test_start_item == 4) { menu_navigate(9); }                      /* Video -> System */
        }
        if (test_frame_count && test_results && frames_shown == test_frame_count - 1) menu_arcade_results();
        if (test_frame_count && test_bossbar && frames_shown == test_frame_count / 2) bossbar_test_engage();
        if (test_frame_count && test_shot && frames_shown == test_frame_count - 4) screenshot();
        if (test_frame_count && test_hotkeys && (frames_shown == test_frame_count - 10 ||
                                                 frames_shown == test_frame_count - 6)) {
            SDL_Event k = {0};
            k.type = SDL_EVENT_KEY_DOWN;
            k.key.down = true;
            k.key.scancode = cfg.hk_key[frames_shown == test_frame_count - 10 ? HK_SLOT_NEXT : HK_SAVE];
            SDL_PushEvent(&k);
        }
        if (test_frame_count && test_mouse && (frames_shown == test_frame_count - 4 || frames_shown == test_frame_count - 2)) {
            int ww, wh;
            SDL_GetWindowSize(window, &ww, &wh);
            float k = wh / 720.0f;
            if (ww / k < 960) k = ww / 960.0f;
            /* canvas units: the second option of the start list; the up arrow of the first card */
            float x = 200, y = 313;
            if (frames_shown == test_frame_count - 2) {
                float cw = (ww / k - 128 - 64) / 3, cx = 64 + cw / 2;   /* three cards */
                x = cx - 60;
                y = 140 + 100 + 12;
            }
            SDL_Event ev = {0};
            ev.type = SDL_EVENT_MOUSE_MOTION;
            ev.motion.windowID = SDL_GetWindowID(window);
            ev.motion.x = x * k;
            ev.motion.y = y * k;
            SDL_PushEvent(&ev);
            SDL_Event c = {0};
            c.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
            c.button.windowID = SDL_GetWindowID(window);
            c.button.button = SDL_BUTTON_LEFT;
            c.button.down = true;
            c.button.clicks = 1;
            c.button.x = x * k;
            c.button.y = y * k;
            SDL_PushEvent(&c);
        }
        if (test_frame_count && test_mission && frames_shown == 1300) {      /* Mission mode, mission N */
            for (int i = fe_resume_available() ? 0 : 1; i < 2; i++) menu_navigate(1);
            menu_navigate(4);                               /* Game modes */
            menu_navigate(3);
            menu_navigate(4);                               /* the Mission card */
            for (int i = 1; i < test_mission; i++) menu_navigate(3);
            menu_navigate(4);
        }
        if (test_frame_count && test_arcade_run && frames_shown == 1300) {
            for (int i = fe_resume_available() ? 0 : 1; i < 2; i++) menu_navigate(1);
            menu_navigate(4);                               /* Game modes: the Arcade card */
            menu_navigate(1);                               /* continues: 3 -> 0 */
            menu_navigate(4);                               /* Start */
        }
        if (test_frame_count && test_rush_run && frames_shown == 1300) {
            for (int i = fe_resume_available() ? 0 : 1; i < 2; i++) menu_navigate(1);
            menu_navigate(4);                               /* Game modes */
            menu_navigate(3);
            menu_navigate(3);                               /* the Boss Rush card */
            menu_navigate(4);
        }
        if (test_frame_count && test_rush_run && arc.over && frames_shown < test_frame_count - 2)
            test_frame_count = frames_shown + 2;
        if (test_frame_count && test_arcade_run && arc.over && frames_shown < test_frame_count - 2)
            test_frame_count = frames_shown + 2;
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
            case SDL_EVENT_KEY_DOWN: on_key(&e.key, &paused); break;
            case SDL_EVENT_TEXT_INPUT: sounds_text(e.text.text); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                latch_pad(e.gbutton.which, e.gbutton.button);
                on_pad(e.gbutton.which, e.gbutton.button, &paused);
                break;
            case SDL_EVENT_GAMEPAD_AXIS_MOTION: on_axis(&e.gaxis, &paused); break;
            case SDL_EVENT_GAMEPAD_ADDED: pad_added(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_REMOVED: pad_removed(e.gdevice.which); break;
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: cfg.fullscreen = 1; hz = monitor_rate(); break;
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: cfg.fullscreen = 0; hz = monitor_rate(); break;
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED: hz = monitor_rate(); break;
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                if (cfg.focus_pause && !test_frame_count) {
                    unfocused = 1;
                    if (audio) SDL_PauseAudioStreamDevice(audio);
                }
                break;
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                if (unfocused && audio) SDL_ResumeAudioStreamDevice(audio);
                unfocused = 0;
                break;
            /* Mouse: the menus (menu.c); in the sound menu only the wheel and going back. */
            case SDL_EVENT_MOUSE_MOTION: case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_WHEEL:
                /* after the gamepad, a tiny nudge (a desk's vibration) does not bring the mouse back */
                if (pad_mode && e.type == SDL_EVENT_MOUSE_MOTION && SDL_fabsf(e.motion.xrel) + SDL_fabsf(e.motion.yrel) < 4)
                    break;
                pad_mode = 0;
                mouse_seen = SDL_GetTicksNS();
                if (menu_capturing() || sounds_editing()) break;
                SDL_ConvertEventToRenderCoordinates(render, &e);
                if (e.type != SDL_EVENT_MOUSE_MOTION) menu_set_gamepad(0);
                if (sounds_open()) {
                    if (e.type == SDL_EVENT_MOUSE_WHEEL && e.wheel.y) sounds_navigate(e.wheel.y > 0 ? 0 : 1);
                    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN && e.button.button == SDL_BUTTON_RIGHT) sounds_navigate(5);
                } else if (menu_open()) {
                    if (e.type == SDL_EVENT_MOUSE_MOTION) menu_mouse_move(e.motion.x, e.motion.y);
                    else if (e.type == SDL_EVENT_MOUSE_WHEEL) menu_mouse_wheel(e.wheel.y);
                    else if (e.button.button == SDL_BUTTON_LEFT) menu_mouse_click(e.button.x, e.button.y);
                    else if (e.button.button == SDL_BUTTON_RIGHT) menu_navigate(5);
                }
                break;
            default: break;
            }
        }
        /* The cursor hides during the game after 2 s without moving the mouse, and while the gamepad
           is in use (pad_mode). */
        {
            int want = !pad_mode && (menu_open() || sounds_open() || SDL_GetTicksNS() - mouse_seen < 2000000000ULL);
            if (want != cursor_shown) {
                if (want) SDL_ShowCursor();
                else SDL_HideCursor();
                cursor_shown = want;
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
        /* Fast-forward: its shortcut held (Tab, the right trigger), x4 and silent (not in Arcade mode). */
        int fast = !arc.active && !menu_open() && !sounds_open() && !paused && hk_held(HK_FAST);
        if (fast) frames *= 4;
        if (unfocused) frames = 0;                    /* paused while the window is not active */
        /* Fast boot: a slice per refresh (the window keeps answering), then the game as usual. */
        int boot_slice = booting() && !sounds_open() && !rush_prep;
        /* Boss Rush's preparation: the fast-forward runs a slice per refresh instead of the game */
        if (rush_prep) {
            frames = 0;
            int r = bossrush_prepare_step(12);
            if (r != 0) {
                rush_prep = 0;
                if (r > 0) fe_rush_start();
                else {
                    fe_reset();
                    menu_start();
                    fe_notify(T("Boss Rush could not be prepared"));
                }
            }
        }
        if (boot_slice) {
            memset(ng.joy, 0, sizeof ng.joy);
            memset(ng.system, 0, sizeof ng.system);
            ng.test = 0x80;
            for (int c = 0; c < 120 && booting(); c++) ng_run_frame();
            frames = 0;
        }
        int real = 0;                                 /* game frames run with the player's inputs */
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
                cheats_frame();
                debug_frame();
                rumble_frame();
                /* Boss Rush skips missions: no unlocks, no mission records */
                int unlocked = arc.rush && arc.active ? 0 : progress_frame();
                if (unlocked) fe_notify(T("Mission %d unlocked"), unlocked);
                if ((unlocked || ng.frames >= resume_next) && resume_worth()) resume_save();   /* autosave */
                /* the game completed in Arcade or Mission mode unlocks Boss Rush */
                if (progress_take_completed() && ((arc.active && !arc.rush) || mission_game) &&
                    bossrush_supported() && !progress_rush_unlocked()) {
                    progress_unlock_rush();
                    fe_notify(T("Boss Rush unlocked!"));
                }
                bossbar_frame();
                if (arc.rush && arc.active) records_invalidate();
                RecordEvent rec;
                if (records_frame(&rec)) {
                    char t[32];
                    records_time_text(rec.frames, t, sizeof t);
                    if (rec.new_time || rec.new_score) fe_notify(T("New record: mission %d in %s"), rec.mission, t);
                    /* a mission that does not count says why (Boss Rush's never count: no notice) */
                    else if (!rec.clean && !(arc.rush && arc.active))
                        fe_notify(T(rec.turbo ? "Record not saved: the slowdown is off (Settings > Game)"
                                              : "Record not saved: cheats, debug options or a loaded game"));
                }
                if (arc.active) arcade_frame();
                step = 0;
                real = 1;
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
        if (audio && (fast || unfocused || boot_slice || rush_prep)) {
            while (ng_audio_read(samples, 4096) > 0) {}   /* fast-forward is silent */
        } else if (audio) {
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
        /* Run-ahead: the image of the frames ahead, after the real frames' audio was queued (loading
           the state back drops pending audio); not with fast-forward, which already skips */
        if (real && cfg.runahead && !fast) runahead_frames(cfg.runahead);
        int w, h;
        SDL_GetRenderOutputSize(render, &w, &h);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
        SDL_RenderClear(render);
        SDL_FRect game = boot_slice || rush_prep ? target_rect(w, h) : draw_game(w, h);   /* the boot: black */
        if (rush_prep) rush_prep_draw(render, w, h);
        if (cfg.boss_bar && !boot_slice && !rush_prep) bossbar_draw(render, game);
        debug_draw(render, game, w, h, cfg.turbo);
        if (sounds_open()) sounds_draw(render, w, h);
        else menu_draw(render, w, h);
        if (fast) fe_notify(T("Fast forward"));
        if (unfocused) fe_notify(T("Paused (window not active)"));
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
    if (rush_prep) bossrush_prepare_cancel();
    if (arc.rush) fe_arcade_stop();    /* Boss Rush from saved states: the player's backup RAM back */
    if (resume_worth()) resume_save();                     /* "Continue" next time */
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
    crt_destroy();
    if (tex_big) SDL_DestroyTexture(tex_big);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(window);
    SDL_Quit();
    ng_roms_free(&ng_roms);
    return 0;
}
