/* mslugx.ini: simple INI format ([section], key=value, comments with ; or #).
 *
 * Keys use SDL names (SDL_GetScancodeName: "Up", "Z", "Left Shift"...) and gamepad buttons use
 * those of SDL_GetGamepadStringForButton ("a", "b", "x", "y", "start", "back", "dpup"..., with
 * the Xbox layout: "a" is the bottom button). An empty key or button leaves the action unbound.
 *
 * Files written by older (Spanish) versions still load: every old section, key, action and value
 * name is accepted as an alias, and the file is rewritten in English on the next save.
 */
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static const char *const ASPECT_NAME[ASPECT_COUNT] = {"4:3", "square"};
static const char *const ASPECT_NAME_OLD[ASPECT_COUNT] = {"4:3", "pixeles"};
static const char *const FILTER_NAME[FILTER_COUNT] = {"sharp", "smooth", "pixel"};
static const char *const FILTER_NAME_OLD[FILTER_COUNT] = {"nitido", "suave", "pixeles"};
static const char *const AUTO_NAME[AUTO_COUNT] = {"off", "button", "fire"};
static const char *const AUTO_NAME_OLD[AUTO_COUNT] = {"no", "boton", "disparo"};
static const char *const SPEED_NAME[AUTO_SPEEDS] = {"10", "7.5", "5"};
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
    static const SDL_GamepadButton b[ACT_COUNT] = {
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

int config_load(Config *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[1200], section[64] = "";
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
            if (is_key(k, "scale", "escala")) c->scale = integer(v, 1, 10, c->scale);
            else if (is_key(k, "fullscreen", "pantalla_completa")) c->fullscreen = integer(v, 0, 1, c->fullscreen);
            else if (is_key(k, "aspect", "aspecto"))
                c->aspect = lookup(ASPECT_NAME, ASPECT_NAME_OLD, ASPECT_COUNT, v, c->aspect);
            else if (is_key(k, "filter", "filtro"))
                c->filter = lookup(FILTER_NAME, FILTER_NAME_OLD, FILTER_COUNT, v, c->filter);
            else if (!strcmp(k, "scanlines")) c->scanlines = integer(v, 0, 1, c->scanlines);
            else if (!strcmp(k, "vsync")) c->vsync = integer(v, 0, 1, c->vsync);
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
            else if (is_key(k, "rumble", "vibracion")) c->rumble = integer(v, 0, 100, c->rumble);
            else if (is_key(k, "start_screen", "pantalla_inicio")) c->start_screen = integer(v, 0, 1, c->start_screen);
            else if (is_key(k, "blood", "sangre")) c->blood = integer(v, 0, 1, c->blood);
        } else if ((j = player_section(section, "keyboard")) >= 0 || (j = player_section(section, "teclado")) >= 0) {
            int a = lookup(ACTION_KEY, ACTION_KEY_OLD, ACT_COUNT, k, -1);
            if (a >= 0) c->key[j][a] = *v ? SDL_GetScancodeFromName(v) : SDL_SCANCODE_UNKNOWN;
        } else if ((j = player_section(section, "gamepad")) >= 0 || (j = player_section(section, "mando")) >= 0) {
            int a = lookup(ACTION_KEY, ACTION_KEY_OLD, ACT_COUNT, k, -1);
            if (a >= 0) c->button[j][a] = *v ? SDL_GetGamepadButtonFromString(v) : SDL_GAMEPAD_BUTTON_INVALID;
        }
    }
    fclose(f);
    return 0;
}

int config_save(const Config *c, const char *path)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "; Metal Slug X (native port): settings. Rewritten on exit.\n\n");
    fprintf(f, "[video]\n; scale: initial window size (x 224 lines)\nscale=%d\nfullscreen=%d\n",
            c->scale, c->fullscreen);
    fprintf(f, "; aspect: 4:3 (like the original monitor) or square (square pixels, 320x224)\naspect=%s\n",
            ASPECT_NAME[c->aspect]);
    fprintf(f, "; filter: sharp (even pixels with soft edges), smooth or pixel\nfilter=%s\nscanlines=%d\n\n",
            FILTER_NAME[c->filter], c->scanlines);
    fprintf(f, "; vsync: 1 = synced to the monitor (no tearing), 0 = exact board timing\n"
               "vsync=%d\n\n", c->vsync);
    fprintf(f, "[audio]\nvolume=%d\n\n", c->volume);
    fprintf(f, "[game]\n; turbo: 0 = like the original (with slowdown), 1 = 68000 at 2x, 2 = at 4x\n");
    fprintf(f, "turbo=%d\nslot=%d\nroms=%s\n", c->turbo, c->slot, c->roms);
    fprintf(f, "; autofire: off, button (its own key or button, \"auto\" in the controls) or fire\n");
    fprintf(f, "autofire1=%s\nautofire2=%s\n", AUTO_NAME[c->autofire[0]], AUTO_NAME[c->autofire[1]]);
    fprintf(f, "; autofire_per_second: 10 (the pistol's maximum), 7.5 or 5\nautofire_per_second=%s\n",
            SPEED_NAME[c->auto_speed]);
    fprintf(f, "; debug: debug panel (F3)\ndebug=%d\n", c->debug);
    fprintf(f, "; rumble: gamepad strength on explosions and shots, 0-100 (0 = off)\nrumble=%d\n",
            c->rumble);
    fprintf(f, "; start_screen: start menu when the game opens (1) or straight into the game (0)\nstart_screen=%d\n",
            c->start_screen);
    fprintf(f, "; blood: 0 = as set in the service menu (BLOOD, off in the US), 1 = always red\n"
               "blood=%d\n", c->blood);
    for (int j = 0; j < 2; j++) {
        fprintf(f, "\n[keyboard%d]\n", j + 1);
        for (int a = 0; a < ACT_COUNT; a++)
            fprintf(f, "%s=%s\n", ACTION_KEY[a],
                    c->key[j][a] != SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeName(c->key[j][a]) : "");
        fprintf(f, "\n[gamepad%d]\n", j + 1);
        for (int a = 0; a < ACT_COUNT; a++) {
            const char *n = c->button[j][a] != SDL_GAMEPAD_BUTTON_INVALID ? SDL_GetGamepadStringForButton(c->button[j][a]) : NULL;
            fprintf(f, "%s=%s\n", ACTION_KEY[a], n ? n : "");
        }
    }
    fclose(f);
    return 0;
}
