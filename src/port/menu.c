/* Port menus (Phase 6C).
 *
 * Stacked pages; each page is a list of options (enum OP_*). An option can open another page,
 * run an action or change a value (left / right). Text uses the game's own font (font.c) over
 * the darkened game:
 *   - Start: the game's demo keeps running in the background, without receiving input;
 *   - Pause: the game is stopped.
 */
#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "cheats.h"
#include "debug.h"
#include "dispatcher.h"
#include "font.h"
#include "frontend.h"
#include "lang.h"
#include "logo.h"
#include "sounds.h"

enum {
    OP_PLAY, OP_CONTINUE, OP_SAVE, OP_LOAD, OP_SETTINGS, OP_SOUNDS, OP_EXTRAS, OP_BACK_TO_START,
    OP_QUIT,
    /* settings */
    OP_LANGUAGE, OP_VIDEO, OP_AUDIO, OP_GAME, OP_CONTROLS,
    OP_FULLSCREEN, OP_VSYNC, OP_ASPECT, OP_FILTER, OP_SCANLINES,
    OP_VOLUME, OP_RUMBLE,
    OP_TURBO, OP_BLOOD, OP_AUTO1, OP_AUTO2, OP_AUTO_SPEED, OP_START_SCREEN,
    OP_CONTROLS1, OP_CONTROLS2,
    /* extras */
    OP_DEBUG, OP_SERVICE, OP_RESET, OP_CHEATS, OP_DEBUG_PAGE,
    /* cheats (in game) */
    OP_CHEAT_PLAYER, OP_GIVE_WEAPON, OP_MAX_BOMBS, OP_LIVES, OP_SOLDIER, OP_FORM,
    OP_INVINCIBLE, OP_INF_BOMBS, OP_INF_AMMO, OP_D_KILLS,
    /* debug */
    OP_DEVMODE, OP_DEV_MENU, OP_BOXES, OP_INFO, OP_GROUND, OP_CPU_METER, OP_PA_POINT, OP_PAUSE_ANY,
};

typedef struct { const char *title; const int *op; int n; } Page;

#define PAGE(name, title, ...) \
    static const int name##_op[] = {__VA_ARGS__}; \
    static const Page name = {title, name##_op, (int)(sizeof name##_op / sizeof(int))}

PAGE(P_START, "METAL SLUG X", OP_PLAY, OP_LOAD, OP_SETTINGS, OP_SOUNDS, OP_EXTRAS, OP_QUIT);
PAGE(P_PAUSE, "PAUSE", OP_CONTINUE, OP_SAVE, OP_LOAD, OP_SETTINGS, OP_SOUNDS, OP_EXTRAS,
     OP_BACK_TO_START, OP_QUIT);
PAGE(P_SETTINGS, "SETTINGS", OP_LANGUAGE, OP_VIDEO, OP_AUDIO, OP_GAME, OP_CONTROLS);
PAGE(P_VIDEO, "VIDEO", OP_FULLSCREEN, OP_VSYNC, OP_ASPECT, OP_FILTER, OP_SCANLINES);
PAGE(P_AUDIO, "AUDIO", OP_VOLUME, OP_RUMBLE);
PAGE(P_GAME, "GAME", OP_TURBO, OP_BLOOD, OP_AUTO1, OP_AUTO2, OP_AUTO_SPEED, OP_START_SCREEN);
PAGE(P_CONTROLS, "CONTROLS", OP_CONTROLS1, OP_CONTROLS2);
PAGE(P_EXTRAS, "EXTRAS", OP_CHEATS, OP_DEBUG_PAGE, OP_DEBUG, OP_SERVICE, OP_RESET);
PAGE(P_CHEATS, "CHEATS", OP_CHEAT_PLAYER, OP_GIVE_WEAPON, OP_MAX_BOMBS, OP_LIVES, OP_SOLDIER, OP_FORM,
     OP_INVINCIBLE, OP_INF_BOMBS, OP_INF_AMMO, OP_D_KILLS);
PAGE(P_DEBUG, "DEBUG", OP_DEVMODE, OP_DEV_MENU, OP_BOXES, OP_INFO, OP_GROUND, OP_CPU_METER,
     OP_PA_POINT, OP_PAUSE_ANY);

/* Cheat selections (session only). */
static int cheat_player, cheat_weapon = 5, cheat_lives = 9;

static struct {
    const Page *stack[6];
    int sel[6];
    int level;                           /* -1 = closed */
    int remap_player, remap_action;      /* -1 = not capturing controls */
} m = {{0}, {0}, -1, -1, 0};

static int cycle(int v, int d, int n) { return ((v + d) % n + n) % n; }

static void open_page(const Page *p)
{
    m.level = 0;
    m.stack[0] = p;
    m.sel[0] = 0;
}

void menu_start(void) { open_page(&P_START); }
void menu_pause(void) { open_page(&P_PAUSE); }
int menu_open(void) { return m.level >= 0; }
int menu_game_runs(void) { return m.level >= 0 && m.stack[0] == &P_START; }

static void close_menu(void)
{
    m.level = -1;
    fe_save_config();
}

static void push_page(const Page *p)
{
    if (m.level + 1 >= (int)(sizeof m.stack / sizeof m.stack[0])) return;
    m.level++;
    m.stack[m.level] = p;
    m.sel[m.level] = 0;
}

static void go_back(void)
{
    if (m.level > 0) { m.level--; fe_save_config(); }
    else if (m.stack[0] == &P_PAUSE) close_menu();    /* on the start screen, Esc does nothing */
}

/* ------------------------------------------------------------------ texts */
/* Texts are written in English; label() and help() translate them (lang.c). */
static const char *label_en(int op)
{
    switch (op) {
    case OP_LANGUAGE: return "Language";
    case OP_PLAY: return "Play";
    case OP_CONTINUE: return "Continue";
    case OP_SAVE: return "Save game";
    case OP_LOAD: return "Load game";
    case OP_SETTINGS: return "Settings";
    case OP_SOUNDS: return "Music and sounds";
    case OP_EXTRAS: return "Extras";
    case OP_BACK_TO_START: return "Back to start screen";
    case OP_QUIT: return "Quit";
    case OP_VIDEO: return "Video";
    case OP_AUDIO: return "Audio";
    case OP_GAME: return "Game";
    case OP_CONTROLS: return "Controls";
    case OP_FULLSCREEN: return "Fullscreen";
    case OP_VSYNC: return "V-sync";
    case OP_ASPECT: return "Aspect";
    case OP_FILTER: return "Filter";
    case OP_SCANLINES: return "Scanlines";
    case OP_VOLUME: return "Volume";
    case OP_RUMBLE: return "Rumble";
    case OP_TURBO: return "Slowdown";
    case OP_BLOOD: return "Blood";
    case OP_AUTO1: return "Autofire P1";
    case OP_AUTO2: return "Autofire P2";
    case OP_AUTO_SPEED: return "Autofire speed";
    case OP_START_SCREEN: return "Start screen";
    case OP_CONTROLS1: return "Player 1";
    case OP_CONTROLS2: return "Player 2";
    case OP_DEBUG: return "Debug panel (F3)";
    case OP_CHEATS: return "Cheats";
    case OP_DEBUG_PAGE: return "Debug";
    case OP_CHEAT_PLAYER: return "Player";
    case OP_GIVE_WEAPON: return "Weapon";
    case OP_MAX_BOMBS: return "Give 99 bombs";
    case OP_LIVES: return "Set lives";
    case OP_SOLDIER: return "Soldier";
    case OP_FORM: return "Form";
    case OP_INVINCIBLE: return "Invincible";
    case OP_INF_BOMBS: return "Infinite bombs";
    case OP_INF_AMMO: return "Infinite ammo";
    case OP_D_KILLS: return "D kills all enemies";
    case OP_DEVMODE: return "Developer mode";
    case OP_DEV_MENU: return "Open developer menu (F4)";
    case OP_BOXES: return "Collision boxes";
    case OP_INFO: return "Game info";
    case OP_GROUND: return "Ground lines";
    case OP_CPU_METER: return "CPU load meter";
    case OP_PA_POINT: return "PA point";
    case OP_PAUSE_ANY: return "Pause anywhere";
    case OP_SERVICE: return "Service menu";
    case OP_RESET: return "Reset the machine";
    default: return "?";
    }
}

static const char *label(int op) { return T(label_en(op)); }

/* Debug dips behind each switch (cheats.h). */
static uint16_t dip_of(int op)
{
    switch (op) {
    case OP_INVINCIBLE: return DIP_INVINCIBLE;
    case OP_INF_BOMBS: return DIP_INFINITE_BOMBS;
    case OP_INF_AMMO: return DIP_INFINITE_AMMO;
    case OP_D_KILLS: return DIP_D_KILLS_ALL;
    case OP_BOXES: return DIP_ATTACK_BOXES | DIP_BODY_BOXES;
    case OP_INFO: return DIP_INFO;
    case OP_GROUND: return DIP_GROUND_A | DIP_GROUND_B;
    case OP_CPU_METER: return DIP_CPU_METER;
    case OP_PA_POINT: return DIP_PA_POINT;
    case OP_PAUSE_ANY: return DIP_PAUSE_ANYTIME;
    default: return 0;
    }
}

/* Value on the right (empty if the option has no value). */
static void value(int op, char *b, size_t n)
{
    static const char *const ASPECT[ASPECT_COUNT] = {"4:3", "Square"};
    static const char *const FILTER[FILTER_COUNT] = {"Sharp", "Smooth", "Pixel"};
    static const char *const AUTO[AUTO_COUNT] = {"Off", "Own button", "On fire"};
    static const char *const SPEED[AUTO_SPEEDS] = {"10/s", "7.5/s", "5/s"};
    static const char *const LANGUAGE[LANG_COUNT] = {"English", "Espanol"};   /* never translated */
    b[0] = 0;
    switch (op) {
    case OP_SAVE: case OP_LOAD: snprintf(b, n, T("< Slot %d >"), cfg.slot); break;
    case OP_LANGUAGE: snprintf(b, n, "%s", LANGUAGE[cfg.language]); break;
    case OP_FULLSCREEN: snprintf(b, n, "%s", T(cfg.fullscreen ? "Yes" : "No")); break;
    case OP_VSYNC: snprintf(b, n, "%s", T(cfg.vsync ? "Yes" : "No")); break;
    case OP_ASPECT: snprintf(b, n, "%s", T(ASPECT[cfg.aspect])); break;
    case OP_FILTER: snprintf(b, n, "%s", T(FILTER[cfg.filter])); break;
    case OP_SCANLINES: snprintf(b, n, "%s", T(cfg.scanlines ? "Yes" : "No")); break;
    case OP_VOLUME: snprintf(b, n, "%d%%", cfg.volume); break;
    case OP_RUMBLE: if (cfg.rumble) snprintf(b, n, "%d%%", cfg.rumble); else snprintf(b, n, "%s", T("Off")); break;
    case OP_TURBO:
        if (cfg.turbo) snprintf(b, n, T("Off (x%d)"), 1 << cfg.turbo);
        else snprintf(b, n, "%s", T("Original"));
        break;
    case OP_BLOOD: snprintf(b, n, "%s", T(cfg.blood ? "Red" : "Original")); break;
    case OP_AUTO1: case OP_AUTO2: snprintf(b, n, "%s", T(AUTO[cfg.autofire[op - OP_AUTO1]])); break;
    case OP_AUTO_SPEED: snprintf(b, n, "%s", SPEED[cfg.auto_speed]); break;
    case OP_START_SCREEN: snprintf(b, n, "%s", T(cfg.start_screen ? "Yes" : "No")); break;
    case OP_DEBUG: snprintf(b, n, "%s", T(debug_enabled() ? "Yes" : "No")); break;
    case OP_CHEAT_PLAYER: snprintf(b, n, "< %d >", cheat_player + 1); break;
    case OP_GIVE_WEAPON: snprintf(b, n, "%s", T(cheats_weapon_name(cheat_weapon))); break;
    case OP_LIVES: snprintf(b, n, "< %d >", cheat_lives); break;
    case OP_SOLDIER: snprintf(b, n, "< %s >", CHEATS_SOLDIER_NAME[cheats_soldier(cheat_player)]); break;
    case OP_FORM: {
        int f = cheats_form(cheat_player);
        snprintf(b, n, "< %s >", T(f == FORM_FAT ? "Fat" : f == FORM_MUMMY ? "Mummy" : "Normal"));
        break;
    }
    case OP_INVINCIBLE: case OP_INF_BOMBS: case OP_INF_AMMO: case OP_D_KILLS:
    case OP_BOXES: case OP_INFO: case OP_GROUND: case OP_CPU_METER: case OP_PA_POINT: case OP_PAUSE_ANY:
        snprintf(b, n, "%s", T((cheats_dips & dip_of(op)) == dip_of(op) ? "Yes" : "No"));
        break;
    case OP_DEVMODE: snprintf(b, n, "%s", T(cfg.devmode ? "Yes" : "No")); break;
    case OP_SETTINGS: case OP_EXTRAS: case OP_VIDEO: case OP_AUDIO: case OP_GAME: case OP_CONTROLS:
    case OP_CHEATS: case OP_DEBUG_PAGE:
        snprintf(b, n, ">");
        break;
    default: break;
    }
}

static const char *help_en(int op)
{
    switch (op) {
    case OP_LANGUAGE: return "Language / Idioma";            /* both: findable in either language */
    case OP_TURBO: return "Original: slows down with many sprites, like the arcade";
    case OP_ASPECT: return "4:3 like the original monitor, or square pixels";
    case OP_VSYNC: return "No tearing; at 60 Hz the game runs 1.4% faster";
    case OP_BLOOD: return "Original: as set in the service menu (BLOOD, off in the US)";
    case OP_FILTER: return "Sharp: even pixels without blurry edges";
    case OP_AUTO1: case OP_AUTO2: return "Own button: B / G on the keyboard, RB on the gamepad";
    case OP_SERVICE: return "BIOS menu: difficulty, lives, time (also with F2)";
    case OP_SOUNDS: return "Listen to the music, voices and effects of the game";
    case OP_DEBUG: return "68000 load, slowdown, sprites, palette, RAM";
    case OP_CHEATS: return "Weapons, bombs, lives, invincibility";
    case OP_DEBUG_PAGE: return "The developers' own debug switches and hidden menu";
    case OP_GIVE_WEAPON: return "</>: choose, Enter/A: give it (during a mission)";
    case OP_SOLDIER: return "Takes effect when the player respawns";
    case OP_FORM: return "Fat lasts until you die or change it; mummy until cured";
    case OP_D_KILLS: return "Press D in game: every enemy on screen dies";
    case OP_DEVMODE: return "The game believes it runs on SNK's development hardware";
    case OP_DEV_MENU: return "Stage select, test stages, sound test, debug dips";
    case OP_PA_POINT: return "Marks a reference point used by the developers";
    case OP_CPU_METER: return "The screen darkens while the 68000 is busy";
    case OP_START_SCREEN: return "Show this menu when the game opens";
    default: return "";
    }
}

static const char *help(int op) { return T(help_en(op)); }

/* ------------------------------------------------------------------ actions */
static void step_percent(int *v, int d)
{
    *v += (d ? d : 1) * 10;
    if (*v > 100) *v = d ? 100 : 0;
    if (*v < 0) *v = 0;
}

/* d = 0: select; -1 / +1: change the value. */
static void do_action(int op, int d)
{
    switch (op) {
    case OP_PLAY: if (!d) { close_menu(); fe_play(); } break;
    case OP_CONTINUE: if (!d) close_menu(); break;
    case OP_SAVE: case OP_LOAD:
        if (d) cfg.slot = cycle(cfg.slot - 1, d, 9) + 1;
        else {
            close_menu();
            if (op == OP_SAVE) fe_state_save();
            else fe_state_load();
        }
        break;
    case OP_SETTINGS: if (!d) push_page(&P_SETTINGS); break;
    case OP_SOUNDS: if (!d) sounds_show(); break;
    case OP_EXTRAS: if (!d) push_page(&P_EXTRAS); break;
    case OP_BACK_TO_START: if (!d) { fe_reset(); menu_start(); } break;
    case OP_QUIT: if (!d) fe_quit(); break;
    case OP_VIDEO: if (!d) push_page(&P_VIDEO); break;
    case OP_AUDIO: if (!d) push_page(&P_AUDIO); break;
    case OP_GAME: if (!d) push_page(&P_GAME); break;
    case OP_CONTROLS: if (!d) push_page(&P_CONTROLS); break;
    case OP_LANGUAGE: cfg.language = cycle(cfg.language, d ? d : 1, LANG_COUNT); lang_set(cfg.language); break;
    case OP_FULLSCREEN: cfg.fullscreen = !cfg.fullscreen; fe_apply_video(); break;
    case OP_VSYNC: cfg.vsync = !cfg.vsync; fe_apply_video(); break;
    case OP_ASPECT: cfg.aspect = cycle(cfg.aspect, d ? d : 1, ASPECT_COUNT); break;
    case OP_FILTER: cfg.filter = cycle(cfg.filter, d ? d : 1, FILTER_COUNT); break;
    case OP_SCANLINES: cfg.scanlines = !cfg.scanlines; break;
    case OP_VOLUME: step_percent(&cfg.volume, d); fe_apply_volume(); break;
    case OP_RUMBLE: step_percent(&cfg.rumble, d); rumble_strength(cfg.rumble); break;
    case OP_TURBO: cfg.turbo = cycle(cfg.turbo, d ? d : 1, 3); cpu_turbo = cfg.turbo; break;
    case OP_BLOOD: cfg.blood = !cfg.blood; fe_apply_blood(); break;
    case OP_AUTO1: case OP_AUTO2:
        cfg.autofire[op - OP_AUTO1] = cycle(cfg.autofire[op - OP_AUTO1], d ? d : 1, AUTO_COUNT);
        break;
    case OP_AUTO_SPEED: cfg.auto_speed = cycle(cfg.auto_speed, d ? d : 1, AUTO_SPEEDS); break;
    case OP_START_SCREEN: cfg.start_screen = !cfg.start_screen; break;
    case OP_CONTROLS1: case OP_CONTROLS2:
        if (!d) { m.remap_player = op - OP_CONTROLS1; m.remap_action = 0; }
        break;
    case OP_DEBUG: debug_enable(!debug_enabled()); cfg.debug = debug_enabled(); break;
    case OP_CHEATS: if (!d) push_page(&P_CHEATS); break;
    case OP_DEBUG_PAGE: if (!d) push_page(&P_DEBUG); break;
    case OP_CHEAT_PLAYER: cheat_player = !cheat_player; break;
    case OP_GIVE_WEAPON:
        if (d) cheat_weapon = cycle(cheat_weapon, d, CHEATS_WEAPONS);
        else if (cheats_give_weapon(cheat_player, cheat_weapon)) close_menu();
        else fe_notify(T("Only during a mission"));
        break;
    case OP_MAX_BOMBS:
        if (d) break;
        if (cheats_set_bombs(cheat_player, 99)) close_menu();
        else fe_notify(T("Only during a mission"));
        break;
    case OP_LIVES:
        if (d) cheat_lives = cycle(cheat_lives, d, 10);
        else if (cheats_set_lives(cheat_player, cheat_lives)) close_menu();
        else fe_notify(T("Only during a mission"));
        break;
    case OP_SOLDIER:
        if (!cheats_set_soldier(cheat_player, cycle(cheats_soldier(cheat_player), d ? d : 1, 4)))
            fe_notify(T("Only during a mission"));
        break;
    case OP_FORM: {
        static const int NEXT[4] = {FORM_FAT, FORM_FAT, FORM_MUMMY, FORM_NORMAL};   /* by current form */
        static const int PREV[4] = {FORM_MUMMY, FORM_MUMMY, FORM_NORMAL, FORM_FAT};
        int f = cheats_form(cheat_player) & 3;
        if (cheats_set_form(cheat_player, d < 0 ? PREV[f] : NEXT[f])) close_menu();
        else fe_notify(T("Only during a mission"));
        break;
    }
    case OP_INVINCIBLE: case OP_INF_BOMBS: case OP_INF_AMMO: case OP_D_KILLS:
    case OP_BOXES: case OP_INFO: case OP_GROUND: case OP_CPU_METER: case OP_PA_POINT: case OP_PAUSE_ANY:
        if ((cheats_dips & dip_of(op)) == dip_of(op)) cheats_dips &= (uint16_t)~dip_of(op);
        else cheats_dips |= dip_of(op);
        cheats_apply();
        break;
    case OP_DEVMODE: cfg.devmode = !cfg.devmode; cheats_devmode = cfg.devmode; cheats_apply(); break;
    case OP_DEV_MENU: if (!d) { close_menu(); fe_dev_menu(); } break;
    case OP_SERVICE: if (!d) { close_menu(); fe_service(); } break;
    case OP_RESET: if (!d) { close_menu(); fe_reset(); } break;
    default: break;
    }
}

void menu_navigate(int t)
{
    if (m.level < 0) return;
    const Page *p = m.stack[m.level];
    int *s = &m.sel[m.level];
    switch (t) {
    case 0: *s = cycle(*s, -1, p->n); break;
    case 1: *s = cycle(*s, 1, p->n); break;
    case 2: do_action(p->op[*s], -1); break;
    case 3: do_action(p->op[*s], 1); break;
    case 4: do_action(p->op[*s], 0); break;
    default: go_back(); break;
    }
}

/* ------------------------------------------------------------------ controls */
int menu_capturing(void) { return m.remap_player >= 0; }

static void remap_next(void)
{
    if (++m.remap_action >= ACT_COUNT) {
        m.remap_player = -1;
        fe_save_config();
        fe_notify(T("Controls saved"));
    }
}

void menu_capture_key(SDL_Scancode sc)
{
    if (sc == SDL_SCANCODE_ESCAPE) { m.remap_action = ACT_COUNT - 1; remap_next(); }
    else if (sc == SDL_SCANCODE_DELETE) remap_next();
    else { cfg.key[m.remap_player][m.remap_action] = sc; remap_next(); }
}

void menu_capture_button(SDL_GamepadButton b)
{
    cfg.button[m.remap_player][m.remap_action] = b;
    remap_next();
}

/* ------------------------------------------------------------------ drawing */
void menu_draw(SDL_Renderer *r, int w, int h)
{
    if (m.level < 0) return;
    /* 480 x 300 virtual canvas fitted to the window. */
    float k = font_factor(w, h);
    SDL_SetRenderScale(r, k, k);
    float vw = (float)w / k, vh = (float)h / k, cx = vw / 2;
    int at_start = m.stack[0] == &P_START;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, at_start && m.level == 0 ? 150 : 195);
    SDL_RenderFillRect(r, NULL);
    char b[128];

    if (m.remap_player >= 0) {
        float y = vh / 2 - 50;
        snprintf(b, sizeof b, T("PLAYER %d CONTROLS"), m.remap_player + 1);
        font_centered(r, cx, y, 2, INK_GOLD, b);
        font_centered(r, cx, y + 30, 1, INK_SILVER, T("Press the key or button for:"));
        font_centered(r, cx, y + 44, 2, INK_SILVER, T(ACTION_LABEL[m.remap_action]));
        const char *c = SDL_GetScancodeName(cfg.key[m.remap_player][m.remap_action]);
        const char *bt = SDL_GetGamepadStringForButton(cfg.button[m.remap_player][m.remap_action]);
        snprintf(b, sizeof b, T("Current: %s / gamepad %s"), *c ? c : "-", bt ? bt : "-");
        font_centered(r, cx, y + 70, 1, INK_GRAY, b);
        font_centered(r, cx, y + 90, 1, INK_GRAY, T("Del: keep current    Esc: finish"));
        SDL_SetRenderScale(r, 1, 1);
        return;
    }

    const Page *p = m.stack[m.level];
    float y;
    SDL_Texture *logo = logo_texture();
    if (at_start && m.level == 0 && logo) {
        float lw, lh;
        SDL_GetTextureSize(logo, &lw, &lh);
        float e = SDL_min(250 / lw, 116 / lh);                /* the title logo, full width */
        SDL_FRect d = {SDL_roundf(cx - lw * e / 2), 6, lw * e, lh * e};
        SDL_RenderTexture(r, logo, NULL, &d);
        y = d.y + d.h + 14;
    } else if (at_start && m.level == 0) {
        font_centered(r, cx, 34, font_fit_scale("METAL SLUG X", 5, vw - 24), INK_GOLD, "METAL SLUG X");
        font_centered(r, cx, 80, 1, INK_SILVER, "SUPER VEHICLE-001");
        y = 112;
    } else {
        font_centered(r, cx, 26, 3, INK_GOLD, T(p->title));
        y = 70;
    }
    /* options panel */
    const float row_h = p->n > 9 ? 18 : 20, panel_w = 360;
    float x0 = cx - panel_w / 2;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 140);
    SDL_FRect panel = {x0 - 10, y - 8, panel_w + 20, p->n * row_h + 12};
    SDL_RenderFillRect(r, &panel);
    for (int i = 0; i < p->n; i++) {
        int op = p->op[i], is_sel = i == m.sel[m.level];
        float yi = y + i * row_h;
        if (is_sel) {
            SDL_SetRenderDrawColor(r, 255, 190, 60, 40);
            SDL_FRect bar = {x0 - 6, yi - 3, panel_w + 12, 18};
            SDL_RenderFillRect(r, &bar);
            font_text(r, x0 - 4, yi, 1.5f, INK_GOLD, ">");
        }
        value(op, b, sizeof b);
        if (b[0] && strcmp(b, ">")) {
            font_text(r, x0 + 12, yi, 1.5f, is_sel ? INK_GOLD : INK_SILVER, label(op));
            font_text(r, x0 + panel_w - font_width(b, 1.5f), yi, 1.5f, is_sel ? INK_GOLD : INK_SILVER, b);
        } else if (at_start && m.level == 0) {
            font_centered(r, cx, yi, 1.5f, is_sel ? INK_GOLD : INK_SILVER, label(op));
        } else {
            font_text(r, x0 + 12, yi, 1.5f, is_sel ? INK_GOLD : INK_SILVER, label(op));
            if (b[0]) font_text(r, x0 + panel_w - 12, yi, 1.5f, INK_GRAY, b);
        }
    }
    /* option help and keys */
    const char *a = help(p->op[m.sel[m.level]]);
    if (a[0]) font_centered(r, cx, vh - 36, 1, INK_SILVER, a);
    font_centered(r, cx, vh - 18, 1, INK_GRAY,
                    T(at_start && m.level == 0 ? "Enter/A: select   </>: change"
                                               : "Enter/A: select   </>: change   Esc/B: back"));
    SDL_SetRenderScale(r, 1, 1);
}
