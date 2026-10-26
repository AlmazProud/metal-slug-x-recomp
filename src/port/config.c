/* <game id>.ini (mslugx.ini, mslug3.ini): simple INI format ([section], key=value, comments with ; or #).
 *
 * Keys use SDL names (SDL_GetScancodeName: "Up", "Z", "Left Shift"...) and gamepad buttons use
 * those of SDL_GetGamepadStringForButton ("a", "b", "x", "y", "start", "back", "dpup"..., with
 * the Xbox layout: "a" is the bottom button; the triggers are "lefttrigger" / "righttrigger"). An
 * empty key or button leaves the action unbound. [shortcuts] and [shortcuts_gamepad] hold the
 * port's own keys (menu, quick save...).
 *
 * Files written by older (Spanish) versions still load: every old section, key, action and value
 * name is accepted as an alias, and the file is rewritten in English on the next save.
 */
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crt.h"
#include "lang.h"
#include "neogeo.h"

const char *const ACTION_KEY[ACT_COUNT] = {
    "up", "down", "left", "right", "a", "b", "c", "d", "start", "coin", "auto",
};
/* Old (Spanish) action names, still accepted when loading. */
static const char *const ACTION_KEY_OLD[ACT_COUNT] = {
    "arriba", "abajo", "izquierda", "derecha", "a", "b", "c", "d", "start", "moneda", "auto",
};
const char *const ACTION_LABEL[ACT_COUNT] = {
    "Up", "Down", "Left", "Right", "A (fire)", "B (jump)", "C (grenade)", "D",
    "Start", "Coin", "Autofire",
};

const char *const HOTKEY_KEY[HK_COUNT] = {
    "menu", "pause", "quick_save", "quick_load", "slot_previous", "slot_next", "fast_forward",
    "screenshot", "fullscreen", "debug_panel", "service", "developer_menu",
};
const char *const HOTKEY_LABEL[HK_COUNT] = {
    "Menu", "Pause", "Quick save", "Quick load", "Previous slot", "Next slot", "Fast forward",
    "Screenshot", "Fullscreen", "Debug panel", "Service button", "Developer menu",
};

static const char *const STICK_NAME[8] = {
    "leftstick_up", "leftstick_down", "leftstick_left", "leftstick_right",
    "rightstick_up", "rightstick_down", "rightstick_left", "rightstick_right",
};

const char *pad_input_name(int in)
{
    if (in >= PAD_LS_UP && in <= PAD_RS_RIGHT) return STICK_NAME[in - PAD_LS_UP];
    if (in == PAD_LT) return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
    if (in == PAD_RT) return SDL_GetGamepadStringForAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
    if (in < 0 || in >= SDL_GAMEPAD_BUTTON_COUNT) return NULL;
    return SDL_GetGamepadStringForButton((SDL_GamepadButton)in);
}

int pad_input_from_name(const char *s)
{
    if (!*s) return PAD_NONE;
    for (int i = 0; i < 8; i++)
        if (!SDL_strcasecmp(s, STICK_NAME[i])) return PAD_LS_UP + i;
    SDL_GamepadAxis ax = SDL_GetGamepadAxisFromString(s);
    if (ax == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) return PAD_LT;
    if (ax == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) return PAD_RT;
    return SDL_GetGamepadButtonFromString(s);             /* SDL_GAMEPAD_BUTTON_INVALID = PAD_NONE */
}

static const char *const ASPECT_NAME[ASPECT_COUNT] = {"4:3", "square"};
static const char *const ASPECT_NAME_OLD[ASPECT_COUNT] = {"4:3", "pixeles"};
static const char *const FILTER_NAME[FILTER_COUNT] = {"sharp", "smooth", "pixel"};
static const char *const FILTER_NAME_OLD[FILTER_COUNT] = {"nitido", "suave", "pixeles"};
static const char *const AUTO_NAME[AUTO_COUNT] = {"off", "button", "fire"};
static const char *const AUTO_NAME_OLD[AUTO_COUNT] = {"no", "boton", "disparo"};
static const char *const SPEED_NAME[AUTO_SPEEDS] = {"10", "7.5", "5"};
static const char *const CRT_NAME[CRT_COUNT] = {"off", "arcade", "tv", "soft"};
static const char *const STYLE_NAME[PAD_STYLE_COUNT] = {"auto", "xbox", "playstation", "nintendo", "arcade"};
static const char *const LANGUAGE_NAME[LANG_COUNT] = {"en", "es"};
const int AUTO_PERIOD[AUTO_SPEEDS] = {6, 8, 12};

void config_defaults(Config *c)
{
    memset(c, 0, sizeof *c);
    c->scale = 3;
    c->aspect = ASPECT_4_3;
    c->filter = FILTER_SHARP;
    c->volume = 100;
    c->slot = 1;
    c->autofire[0] = c->autofire[1] = AUTO_BUTTON;
    c->rumble = 70;
    c->vsync = 1;
    c->start_screen = 1;
    c->focus_pause = 1;
    c->fast_boot = 1;
    c->boss_bar = 1;
    c->crt_curve = 1;
    c->pad_hides_cursor = 1;
    static const SDL_Scancode t1[ACT_COUNT] = {
        SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_1, SDL_SCANCODE_5,
        SDL_SCANCODE_B,
    };
    static const SDL_Scancode t2[ACT_COUNT] = {
        SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L,
        SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_2, SDL_SCANCODE_6,
        SDL_SCANCODE_G,
    };
    static const int b[ACT_COUNT] = {
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_SOUTH,
        SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    };
    for (int i = 0; i < ACT_COUNT; i++) {
        c->key[0][i] = t1[i];
        c->key[1][i] = t2[i];
        c->button[0][i] = c->button[1][i] = b[i];
    }
    /* shortcuts: the keys the port always had (F7 / F8 for the slot are new); on the gamepad the
       Guide button, the right trigger and Share / Capture, the rest unbound */
    static const SDL_Scancode hk[HK_COUNT] = {
        SDL_SCANCODE_ESCAPE, SDL_SCANCODE_P, SDL_SCANCODE_F5, SDL_SCANCODE_F9, SDL_SCANCODE_F7, SDL_SCANCODE_F8,
        SDL_SCANCODE_TAB, SDL_SCANCODE_F12, SDL_SCANCODE_F11, SDL_SCANCODE_F3, SDL_SCANCODE_F2, SDL_SCANCODE_F4,
    };
    for (int h = 0; h < HK_COUNT; h++) {
        c->hk_key[h] = hk[h];
        c->hk_pad[h] = PAD_NONE;
    }
    for (int k = 0; k < PAD_PROFILES; k++) {
        memcpy(c->profile[k].button, b, sizeof c->profile[k].button);
        c->profile[k].style = PAD_STYLE_AUTO;
        c->profile[k].guid[0] = 0;
    }
    c->pad_profile[0] = 0;
    c->pad_profile[1] = 1;
    c->hk_pad[HK_MENU] = SDL_GAMEPAD_BUTTON_GUIDE;
    c->hk_pad[HK_FAST] = PAD_RT;
    c->hk_pad[HK_SCREENSHOT] = SDL_GAMEPAD_BUTTON_MISC1;
}

static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

/* Index of `v` in `names` or, failing that, in `old_names` (may be NULL); `current` if absent. */
static int lookup(const char *const *names, const char *const *old_names, int n, const char *v, int current)
{
    for (int i = 0; i < n; i++)
        if (!SDL_strcasecmp(names[i], v)) return i;
    if (old_names)
        for (int i = 0; i < n; i++)
            if (!SDL_strcasecmp(old_names[i], v)) return i;
    return current;
}

static int integer(const char *v, int min, int max, int current)
{
    char *end;
    long n = strtol(v, &end, 10);
    if (end == v) return current;
    return n < min ? min : n > max ? max : (int)n;
}

/* Key `k` is `name` or its old (Spanish) alias. */
static int is_key(const char *k, const char *name, const char *old_name)
{
    return !strcmp(k, name) || !strcmp(k, old_name);
}

/* "keyboard1" / "teclado1" -> 0, "gamepad2" / "mando2" -> 1... (-1 if `s` does not start with
   `prefix` followed by 1 or 2). */
static int player_section(const char *s, const char *prefix)
{
    size_t n = strlen(prefix);
    if (strncmp(s, prefix, n) || (s[n] != '1' && s[n] != '2')) return -1;
    return s[n] - '1';
}

void config_profile_load(Config *c, int player)
{
    const PadProfile *p = &c->profile[c->pad_profile[player & 1]];
    memcpy(c->button[player & 1], p->button, sizeof p->button);
    c->pad_style[player & 1] = p->style;
}

void config_profile_store(Config *c, int player)
{
    int j = player & 1;
    PadProfile *p = &c->profile[c->pad_profile[j]];
    memcpy(p->button, c->button[j], sizeof p->button);
    p->style = c->pad_style[j];
    if (c->pad_profile[!j] == c->pad_profile[j]) config_profile_load(c, !j);
}

int config_profile_of(const Config *c, const char *guid)
{
    for (int k = 0; guid && *guid && k < PAD_PROFILES; k++)
        if (!strcmp(c->profile[k].guid, guid)) return k;
    return -1;
}

/* "gamepad_profile1" -> 0... (-1 if not a profile section). */
static int profile_section(const char *s)
{
    static const char P[] = "gamepad_profile";
    if (strncmp(s, P, sizeof P - 1) || s[sizeof P - 1] < '1' || s[sizeof P - 1] > '0' + PAD_PROFILES || s[sizeof P])
        return -1;
    return s[sizeof P - 1] - '1';
}

int config_load(Config *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1200], section[64] = "";
    int profiles = 0;                    /* the file has profiles (older ones: each player's mapping) */
    while (fgets(line, sizeof line, f)) {
        char *l = trim(line);
        if (!*l || *l == ';' || *l == '#') continue;
        if (*l == '[') {
            char *end = strchr(l, ']');
            if (end) { *end = 0; snprintf(section, sizeof section, "%s", trim(l + 1)); }
            continue;
        }
        char *eq = strchr(l, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(l), *v = trim(eq + 1);
        int j;
        if (!strcmp(section, "video")) {
            if (is_key(k, "scale", "escala")) c->scale = integer(v, 1, 16, c->scale);
            else if (is_key(k, "fullscreen", "pantalla_completa")) c->fullscreen = integer(v, 0, 1, c->fullscreen);
            else if (is_key(k, "aspect", "aspecto"))
                c->aspect = lookup(ASPECT_NAME, ASPECT_NAME_OLD, ASPECT_COUNT, v, c->aspect);
            else if (is_key(k, "filter", "filtro"))
                c->filter = lookup(FILTER_NAME, FILTER_NAME_OLD, FILTER_COUNT, v, c->filter);
            else if (!strcmp(k, "scanlines")) c->scanlines = integer(v, 0, 1, c->scanlines);
            else if (!strcmp(k, "vsync")) c->vsync = integer(v, 0, 1, c->vsync);
            else if (!strcmp(k, "integer_scale")) c->integer_scale = integer(v, 0, 1, c->integer_scale);
            else if (!strcmp(k, "crt")) c->crt = lookup(CRT_NAME, NULL, CRT_COUNT, v, c->crt);
            else if (!strcmp(k, "crt_curve")) c->crt_curve = integer(v, 0, 1, c->crt_curve);
        } else if (!strcmp(section, "audio")) {
            if (is_key(k, "volume", "volumen")) c->volume = integer(v, 0, 100, c->volume);
        } else if (!strcmp(section, "game") || !strcmp(section, "juego")) {
            if (!strcmp(k, "turbo")) c->turbo = integer(v, 0, 2, c->turbo);
            else if (is_key(k, "slot", "ranura")) c->slot = integer(v, 1, 9, c->slot);
            else if (!strcmp(k, "roms")) snprintf(c->roms, sizeof c->roms, "%s", v);
            else if (is_key(k, "autofire1", "autodisparo1"))
                c->autofire[0] = lookup(AUTO_NAME, AUTO_NAME_OLD, AUTO_COUNT, v, c->autofire[0]);
            else if (is_key(k, "autofire2", "autodisparo2"))
                c->autofire[1] = lookup(AUTO_NAME, AUTO_NAME_OLD, AUTO_COUNT, v, c->autofire[1]);
            else if (is_key(k, "autofire_per_second", "autodisparo_por_segundo"))
                c->auto_speed = lookup(SPEED_NAME, NULL, AUTO_SPEEDS, v, c->auto_speed);
            else if (!strcmp(k, "debug")) c->debug = integer(v, 0, 1, c->debug);
            else if (!strcmp(k, "devmode")) c->devmode = integer(v, 0, 1, c->devmode);
            else if (is_key(k, "rumble", "vibracion")) c->rumble = integer(v, 0, 100, c->rumble);
            else if (is_key(k, "start_screen", "pantalla_inicio")) c->start_screen = integer(v, 0, 1, c->start_screen);
            else if (is_key(k, "blood", "sangre")) c->blood = integer(v, 0, 1, c->blood);
            else if (!strcmp(k, "focus_pause")) c->focus_pause = integer(v, 0, 1, c->focus_pause);
            else if (!strcmp(k, "fast_boot")) c->fast_boot = integer(v, 0, 1, c->fast_boot);
            else if (!strcmp(k, "boss_bar")) c->boss_bar = integer(v, 0, 1, c->boss_bar);
            else if (!strcmp(k, "runahead")) c->runahead = integer(v, 0, 2, c->runahead);
            else if (!strcmp(k, "gamepad_hides_cursor")) c->pad_hides_cursor = integer(v, 0, 1, c->pad_hides_cursor);
            else if (is_key(k, "language", "idioma"))
                c->language = lookup(LANGUAGE_NAME, NULL, LANG_COUNT, v, c->language);
        } else if ((j = player_section(section, "keyboard")) >= 0 || (j = player_section(section, "teclado")) >= 0) {
            int a = lookup(ACTION_KEY, ACTION_KEY_OLD, ACT_COUNT, k, -1);
            if (a >= 0) c->key[j][a] = *v ? SDL_GetScancodeFromName(v) : SDL_SCANCODE_UNKNOWN;
        } else if ((j = profile_section(section)) >= 0) {
            PadProfile *p = &c->profile[j];
            int a = lookup(ACTION_KEY, NULL, ACT_COUNT, k, -1);
            profiles = 1;
            if (a >= 0) p->button[a] = pad_input_from_name(v);
            else if (!strcmp(k, "style")) p->style = lookup(STYLE_NAME, NULL, PAD_STYLE_COUNT, v, p->style);
            else if (!strcmp(k, "device")) snprintf(p->guid, sizeof p->guid, "%s", v);
        } else if ((j = player_section(section, "gamepad")) >= 0 || (j = player_section(section, "mando")) >= 0) {
            int a = lookup(ACTION_KEY, ACTION_KEY_OLD, ACT_COUNT, k, -1);
            if (a >= 0) c->button[j][a] = pad_input_from_name(v);
            else if (!strcmp(k, "device")) snprintf(c->pad_guid[j], sizeof c->pad_guid[j], "%s", v);
            else if (!strcmp(k, "style")) c->pad_style[j] = lookup(STYLE_NAME, NULL, PAD_STYLE_COUNT, v, c->pad_style[j]);
            else if (!strcmp(k, "profile")) c->pad_profile[j] = integer(v, 1, PAD_PROFILES, c->pad_profile[j] + 1) - 1;
        } else if (!strcmp(section, "shortcuts")) {
            int h = lookup(HOTKEY_KEY, NULL, HK_COUNT, k, -1);
            if (h >= 0) c->hk_key[h] = *v ? SDL_GetScancodeFromName(v) : SDL_SCANCODE_UNKNOWN;
        } else if (!strcmp(section, "shortcuts_gamepad")) {
            int h = lookup(HOTKEY_KEY, NULL, HK_COUNT, k, -1);
            if (h >= 0) c->hk_pad[h] = pad_input_from_name(v);
        }
    }
    fclose(f);
    /* the working copies come from the profiles; a file without them gives each player's
       mapping to profiles 1 and 2 */
    for (int j = 0; j < 2; j++) {
        if (profiles) config_profile_load(c, j);
        else {
            c->pad_profile[j] = j;
            config_profile_store(c, j);
        }
    }
    return 0;
}

int config_save(const Config *c, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "; %s (native port): settings. Rewritten on exit.\n\n", ng_game->title);
    fprintf(f, "[video]\n; scale: initial window size (x 224 lines)\nscale=%d\nfullscreen=%d\n",
            c->scale, c->fullscreen);
    fprintf(f, "; aspect: 4:3 (like the original monitor) or square (square pixels, 320x224)\naspect=%s\n",
            ASPECT_NAME[c->aspect]);
    fprintf(f, "; filter: sharp (even pixels with soft edges), smooth or pixel\nfilter=%s\nscanlines=%d\n\n",
            FILTER_NAME[c->filter], c->scanlines);
    fprintf(f, "; vsync: 1 = synced to the monitor (no tearing), 0 = exact board timing\n"
               "vsync=%d\n", c->vsync);
    fprintf(f, "; integer_scale: 1 = the image at a whole multiple of 224 lines (sharpest on 4K/8K)\n"
               "integer_scale=%d\n", c->integer_scale);
    fprintf(f, "; crt: CRT effect, off, arcade (aperture grille), tv (slot mask) or soft (lines only)\n"
               "crt=%s\n; crt_curve: 1 = the CRT effect's screen is curved\ncrt_curve=%d\n\n", CRT_NAME[c->crt], c->crt_curve);
    fprintf(f, "[audio]\nvolume=%d\n\n", c->volume);
    fprintf(f, "[game]\n; turbo: 0 = like the original (with slowdown), 1 = 68000 at 2x, 2 = at 4x\n");
    fprintf(f, "turbo=%d\nslot=%d\nroms=%s\n", c->turbo, c->slot, c->roms);
    fprintf(f, "; autofire: off, button (its own key or button, \"auto\" in the controls) or fire\n");
    fprintf(f, "autofire1=%s\nautofire2=%s\n", AUTO_NAME[c->autofire[0]], AUTO_NAME[c->autofire[1]]);
    fprintf(f, "; autofire_per_second: 10 (the pistol's maximum), 7.5 or 5\nautofire_per_second=%s\n",
            SPEED_NAME[c->auto_speed]);
    fprintf(f, "; debug: debug panel (F3)\ndebug=%d\n", c->debug);
    fprintf(f, "; devmode: developer mode, the game's hidden menu (F4 in game)\ndevmode=%d\n", c->devmode);
    fprintf(f, "; rumble: gamepad strength on explosions and shots, 0-100 (0 = off)\nrumble=%d\n",
            c->rumble);
    fprintf(f, "; start_screen: start menu when the game opens (1) or straight into the game (0)\nstart_screen=%d\n",
            c->start_screen);
    fprintf(f, "; blood: 0 = as set in the service menu (BLOOD, off in the US), 1 = always red\n"
               "blood=%d\n", c->blood);
    fprintf(f, "; focus_pause: 1 = the game pauses while the window is not active\nfocus_pause=%d\n",
            c->focus_pause);
    fprintf(f, "; fast_boot: 1 = the BIOS boot runs unseen and as fast as possible\nfast_boot=%d\n", c->fast_boot);
    fprintf(f, "; boss_bar: 1 = the bosses' health bar (drawn over the game)\nboss_bar=%d\n", c->boss_bar);
    fprintf(f, "; runahead: 0 = off, 1 or 2 = the game's reaction shown that many frames sooner\nrunahead=%d\n",
            c->runahead);
    fprintf(f, "; gamepad_hides_cursor: 1 = using the gamepad hides the mouse cursor until the mouse or a key is used\n"
               "gamepad_hides_cursor=%d\n", c->pad_hides_cursor);
    fprintf(f, "; language: menu language, en (English) or es (Spanish)\nlanguage=%s\n",
            LANGUAGE_NAME[c->language]);
    for (int j = 0; j < 2; j++) {
        fprintf(f, "\n[keyboard%d]\n", j + 1);
        for (int a = 0; a < ACT_COUNT; a++)
            fprintf(f, "%s=%s\n", ACTION_KEY[a],
                    c->key[j][a] != SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeName(c->key[j][a]) : "");
        fprintf(f, "\n[gamepad%d]\n; device: the gamepad this player uses (empty: in connection order)\ndevice=%s\n"
                   "; profile: the gamepad profile (1-%d) this player uses\nprofile=%d\n",
                j + 1, c->pad_guid[j], PAD_PROFILES, c->pad_profile[j] + 1);
    }
    for (int k = 0; k < PAD_PROFILES; k++) {
        const PadProfile *p = &c->profile[k];
        fprintf(f, "\n[gamepad_profile%d]\n; style: auto, xbox, playstation, nintendo or arcade; device: the gamepad "
                   "it loads with\nstyle=%s\ndevice=%s\n", k + 1, STYLE_NAME[p->style], p->guid);
        for (int a = 0; a < ACT_COUNT; a++) {
            const char *n = pad_input_name(p->button[a]);
            fprintf(f, "%s=%s\n", ACTION_KEY[a], n ? n : "");
        }
    }
    fprintf(f, "\n; shortcuts of the port, common to both players (empty = unbound)\n[shortcuts]\n");
    for (int h = 0; h < HK_COUNT; h++)
        fprintf(f, "%s=%s\n", HOTKEY_KEY[h], c->hk_key[h] != SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeName(c->hk_key[h]) : "");
    fprintf(f, "\n[shortcuts_gamepad]\n");
    for (int h = 0; h < HK_COUNT; h++) {
        const char *n = pad_input_name(c->hk_pad[h]);
        fprintf(f, "%s=%s\n", HOTKEY_KEY[h], n ? n : "");
    }
    fclose(f);
    return 0;
}
