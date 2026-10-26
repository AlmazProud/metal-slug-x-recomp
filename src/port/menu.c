/* Port menus, "remaster" style (the design approved by the user: docs/QOL_FEATURES.md).
 *
 * Stacked pages; each page is a list of options (enum OP_*). An option can open another page,
 * run an action or change a value (left / right). Four layouts:
 *   - L_HOME: the start screen (the game's demo keeps running behind, without input) and the
 *     pause (the game is stopped): the list on the left, a detail panel on the right;
 *   - L_LIST: settings (as tabs), extras, cheats, debug, saves and the Arcade results: rows with
 *     their values and the selected option's help on the right; a long list scrolls. The controls
 *     tabs (Keyboard, Gamepad) list every action and shortcut with its key or button: Enter
 *     waits for the new one (a single row changes), Del / Y clears it, a key or button already
 *     in use swaps with the row's old one;
 *   - L_MODES: the game modes as cards (left / right moves, up / down changes the continues);
 *   - L_MISSIONS: Mission mode, one card per mission.
 * Everything is drawn on a virtual canvas 720 units tall (at least 960 wide) fitted to the
 * window, with the menu fonts (ui_font.c), so it stays sharp at any resolution.
 * Mouse: drawing records the clickable areas (options, value arrows, tabs, key hints); moving
 * over an option selects it, a click does what Enter does, the right button goes back.
 */
#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "cheats.h"
#include "crt.h"
#include "debug.h"
#include "dispatcher.h"
#include "font.h"
#include "frontend.h"
#include "lang.h"
#include "logo.h"
#include "profile.h"
#include "bossbar.h"
#include "bossrush.h"
#include "progress.h"
#include "records.h"
#include "sounds.h"
#include "ui_font.h"

enum {
    OP_PLAY, OP_CONTINUE, OP_SAVE, OP_LOAD, OP_SETTINGS, OP_SOUNDS, OP_EXTRAS, OP_BACK_TO_START,
    OP_QUIT, OP_MODES, OP_SAVES,
    /* start screen modes */
    OP_RESUME, OP_ARCADE, OP_MISSIONS, OP_BOSS_RUSH,
    OP_RES_SCORE, OP_RES_TIME, OP_RES_CONT, OP_RES_BEST, OP_RES_BOSSES, OP_RES_RUSH_BEST, OP_RES_DONE,
    /* records */
    OP_RECORDS, OP_REC_RUSH, OP_REC_RUSH_BOSSES, OP_REC_ARCADE, OP_REC_FURTHEST, OP_REC_M1, OP_REC_LAST = OP_REC_M1 + 7,
    OP_MISSION_1, OP_MISSION_LAST = OP_MISSION_1 + 7,
    OP_INTEGER, OP_WINDOW_SIZE, OP_FOCUS_PAUSE, OP_FAST_BOOT, OP_BOSS_BAR, OP_RUNAHEAD, OP_PAD_CURSOR,
    /* settings */
    OP_LANGUAGE, OP_FULLSCREEN, OP_VSYNC, OP_ASPECT, OP_FILTER, OP_SCANLINES, OP_CRT, OP_CRT_CURVE,
    OP_VOLUME, OP_RUMBLE,
    OP_TURBO, OP_BLOOD, OP_AUTO1, OP_AUTO2, OP_AUTO_SPEED, OP_START_SCREEN,
    /* controls (Settings > Keyboard / Gamepad): the player, the game's actions, the shortcuts */
    OP_BIND_PLAYER, OP_BIND_DEVICE, OP_BIND_PROFILE, OP_BIND_STYLE, OP_BIND_HEAD_GAME, OP_BIND_ACT, OP_BIND_ACT_LAST = OP_BIND_ACT + ACT_COUNT - 1,
    OP_BIND_HEAD_HK, OP_BIND_HK, OP_BIND_HK_LAST = OP_BIND_HK + HK_COUNT - 1, OP_BIND_RESET,
    /* extras */
    OP_DEBUG, OP_SERVICE, OP_RESET, OP_CHEATS, OP_DEBUG_PAGE,
    /* cheats (in game) */
    OP_START_MISSION, OP_CHEAT_PLAYER, OP_GIVE_WEAPON, OP_VEHICLE, OP_MAX_BOMBS, OP_LIVES, OP_SOLDIER, OP_FORM,
    OP_INVINCIBLE, OP_INF_BOMBS, OP_INF_AMMO, OP_D_KILLS,
    /* debug */
    OP_DEVMODE, OP_DEV_MENU, OP_BOXES, OP_INFO, OP_GROUND, OP_CPU_METER, OP_PA_POINT, OP_PAUSE_ANY,
};

enum { L_HOME, L_LIST, L_MODES, L_MISSIONS };

typedef struct { const char *title; const int *op; int n; int layout; int tabs; } Page;

#define PAGE(name, layout, tabs, title, ...) \
    static const int name##_op[] = {__VA_ARGS__}; \
    static const Page name = {title, name##_op, (int)(sizeof name##_op / sizeof(int)), layout, tabs}

PAGE(P_START, L_HOME, 0, "", OP_RESUME, OP_PLAY, OP_MODES, OP_LOAD, OP_SETTINGS, OP_EXTRAS, OP_QUIT);
PAGE(P_PAUSE, L_HOME, 0, "PAUSE", OP_CONTINUE, OP_SAVES, OP_SETTINGS, OP_EXTRAS, OP_BACK_TO_START, OP_QUIT);
PAGE(P_MODES, L_MODES, 0, "GAME MODES", OP_ARCADE, OP_MISSIONS, OP_BOSS_RUSH);
PAGE(P_MISSIONS, L_MISSIONS, 0, "MISSION MODE", OP_MISSION_1, OP_MISSION_1 + 1, OP_MISSION_1 + 2, OP_MISSION_1 + 3,
     OP_MISSION_1 + 4, OP_MISSION_1 + 5, OP_MISSION_1 + 6, OP_MISSION_1 + 7);
PAGE(P_RESULTS, L_LIST, 0, "GAME OVER", OP_RES_SCORE, OP_RES_TIME, OP_RES_CONT, OP_RES_BEST, OP_RES_DONE);
PAGE(P_RUSH_RESULTS, L_LIST, 0, "BOSS RUSH", OP_RES_BOSSES, OP_RES_TIME, OP_RES_SCORE, OP_RES_RUSH_BEST, OP_RES_DONE);
PAGE(P_RECORDS, L_LIST, 0, "RECORDS", OP_REC_RUSH, OP_REC_RUSH_BOSSES, OP_REC_ARCADE, OP_REC_FURTHEST, OP_REC_M1, OP_REC_M1 + 1, OP_REC_M1 + 2,
     OP_REC_M1 + 3, OP_REC_M1 + 4, OP_REC_M1 + 5, OP_REC_M1 + 6, OP_REC_M1 + 7);
PAGE(P_SAVES, L_LIST, 0, "SAVES", OP_SAVE, OP_LOAD);
/* Settings: one tab each. */
PAGE(P_VIDEO, L_LIST, 1, "VIDEO", OP_FULLSCREEN, OP_WINDOW_SIZE, OP_ASPECT, OP_CRT, OP_CRT_CURVE, OP_FILTER, OP_INTEGER, OP_SCANLINES,
     OP_VSYNC);
PAGE(P_AUDIO, L_LIST, 1, "AUDIO", OP_VOLUME, OP_RUMBLE);
PAGE(P_GAME, L_LIST, 1, "GAME", OP_BOSS_BAR, OP_BLOOD, OP_TURBO, OP_RUNAHEAD, OP_AUTO1, OP_AUTO2, OP_AUTO_SPEED);
PAGE(P_KEYBOARD, L_LIST, 1, "KEYBOARD", OP_BIND_PLAYER, OP_BIND_DEVICE, OP_BIND_RESET, OP_BIND_HEAD_GAME, OP_BIND_ACT + 0, OP_BIND_ACT + 1, OP_BIND_ACT + 2, OP_BIND_ACT + 3, OP_BIND_ACT + 4, OP_BIND_ACT + 5, OP_BIND_ACT + 6, OP_BIND_ACT + 7, OP_BIND_ACT + 8, OP_BIND_ACT + 9, OP_BIND_ACT + 10, OP_BIND_HEAD_HK, OP_BIND_HK + 0, OP_BIND_HK + 1, OP_BIND_HK + 2, OP_BIND_HK + 3, OP_BIND_HK + 4, OP_BIND_HK + 5, OP_BIND_HK + 6, OP_BIND_HK + 7, OP_BIND_HK + 8, OP_BIND_HK + 9, OP_BIND_HK + 10, OP_BIND_HK + 11);
PAGE(P_GAMEPAD, L_LIST, 1, "GAMEPAD", OP_BIND_PLAYER, OP_BIND_DEVICE, OP_BIND_PROFILE, OP_BIND_STYLE, OP_BIND_RESET, OP_BIND_HEAD_GAME, OP_BIND_ACT + 0, OP_BIND_ACT + 1, OP_BIND_ACT + 2, OP_BIND_ACT + 3, OP_BIND_ACT + 4, OP_BIND_ACT + 5, OP_BIND_ACT + 6, OP_BIND_ACT + 7, OP_BIND_ACT + 8, OP_BIND_ACT + 9, OP_BIND_ACT + 10, OP_BIND_HEAD_HK, OP_BIND_HK + 0, OP_BIND_HK + 1, OP_BIND_HK + 2, OP_BIND_HK + 3, OP_BIND_HK + 4, OP_BIND_HK + 5, OP_BIND_HK + 6, OP_BIND_HK + 7, OP_BIND_HK + 8, OP_BIND_HK + 9, OP_BIND_HK + 10, OP_BIND_HK + 11);
PAGE(P_SYSTEM, L_LIST, 1, "SYSTEM", OP_LANGUAGE, OP_START_SCREEN, OP_FAST_BOOT, OP_FOCUS_PAUSE, OP_PAD_CURSOR);
PAGE(P_EXTRAS, L_LIST, 0, "EXTRAS", OP_SOUNDS, OP_RECORDS, OP_CHEATS, OP_DEBUG_PAGE, OP_SERVICE, OP_RESET);
PAGE(P_CHEATS, L_LIST, 0, "CHEATS", OP_START_MISSION, OP_CHEAT_PLAYER, OP_GIVE_WEAPON, OP_VEHICLE, OP_MAX_BOMBS, OP_LIVES,
     OP_SOLDIER, OP_FORM, OP_INVINCIBLE, OP_INF_BOMBS, OP_INF_AMMO, OP_D_KILLS);
PAGE(P_DEBUG, L_LIST, 0, "DEBUG", OP_DEBUG, OP_DEVMODE, OP_DEV_MENU, OP_BOXES, OP_INFO, OP_GROUND, OP_CPU_METER,
     OP_PA_POINT, OP_PAUSE_ANY);

static const Page *const SETTINGS_TABS[] = {&P_VIDEO, &P_AUDIO, &P_GAME, &P_KEYBOARD, &P_GAMEPAD, &P_SYSTEM};
#define N_TABS ((int)(sizeof SETTINGS_TABS / sizeof SETTINGS_TABS[0]))
static int last_tab;

/* Cheat selections (session only). */
static int cheat_player, cheat_weapon = 5, cheat_lives = 9, cheat_vehicle;
/* Arcade mode continues: 0, 1, 3, 5 or infinite. */
static const int ARC_CONTINUES[] = {0, 1, 3, 5, -1};
#define N_ARC_CONTINUES ((int)(sizeof ARC_CONTINUES / sizeof ARC_CONTINUES[0]))
static int arc_choice = 2;                       /* 3 */
static int gamepad_hints;

/* The selected player's form in the profile's list; any other form (normal with a weapon, a
   suit) shows as the first one, normal. */
static int form_index(void)
{
    int f = cheats_form(cheat_player);
    for (int i = 1; i < game_profile.n_forms; i++)
        if (game_profile.forms[i].form == f) return i;
    return 0;
}

static struct {
    const Page *stack[6];
    int sel[6];
    int first[6];                        /* a long list's first row shown */
    int level;                           /* -1 = closed */
    int capture;                         /* the controls' row waiting for a key or button, -1 none */
    Uint64 capture_until;                /* the gamepad's wait ends (SDL_GetTicks) */
} m = {{0}, {0}, {0}, -1, -1, 0};

#define CAPTURE_MS 6000                  /* waiting for a gamepad button: cancelled after 6 s */
static int bind_player;                  /* the controls tabs' player (0 / 1) */

static int cycle(int v, int d, int n) { return ((v + d) % n + n) % n; }

static void open_page(const Page *p)
{
    m.level = 0;
    m.stack[0] = p;
    m.sel[0] = 0;
}

static int page_ops(const Page *p, int out[32]);

void menu_start(void) { open_page(&P_START); }
void menu_arcade_results(void)
{
    int o[32];
    const Page *p = fe_rush_mode() ? &P_RUSH_RESULTS : &P_RESULTS;
    open_page(p);
    m.sel[0] = page_ops(p, o) - 1;                    /* "Back to start screen" */
}
void menu_pause(void) { open_page(&P_PAUSE); }
int menu_open(void) { return m.level >= 0; }
int menu_game_runs(void) { return m.level >= 0 && m.stack[0] == &P_START; }
void menu_set_gamepad(int on) { gamepad_hints = on; }

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

static int tab_of(const Page *p)
{
    for (int i = 0; i < N_TABS; i++)
        if (SETTINGS_TABS[i] == p) return i;
    return -1;
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
    case OP_MODES: return "Game modes";
    case OP_SAVES: return "Saves";
    case OP_FULLSCREEN: return "Fullscreen";
    case OP_VSYNC: return "V-sync";
    case OP_ASPECT: return "Aspect";
    case OP_FILTER: return "Filter";
    case OP_SCANLINES: return "Scanlines";
    case OP_CRT: return "CRT effect";
    case OP_CRT_CURVE: return "Curvature";
    case OP_VOLUME: return "Volume";
    case OP_RUMBLE: return "Rumble";
    case OP_TURBO: return "Slowdown";
    case OP_BLOOD: return "Blood";
    case OP_AUTO1: return "Autofire P1";
    case OP_AUTO2: return "Autofire P2";
    case OP_AUTO_SPEED: return "Autofire speed";
    case OP_START_SCREEN: return "Start screen";
    case OP_BIND_PLAYER: return "Player";
    case OP_BIND_DEVICE: return "Gamepad";
    case OP_BIND_STYLE: return "Gamepad type";
    case OP_BIND_PROFILE: return "Profile";
    case OP_BIND_HEAD_GAME: return "Game controls";
    case OP_BIND_HEAD_HK: return "Shortcuts";
    case OP_BIND_RESET: return "Restore defaults";
    case OP_DEBUG: return "Debug panel (F3)";
    case OP_CHEATS: return "Cheats";
    case OP_DEBUG_PAGE: return "Debug";
    case OP_START_MISSION: return "Start mission";
    case OP_CHEAT_PLAYER: return "Player";
    case OP_GIVE_WEAPON: return "Weapon";
    case OP_MAX_BOMBS: return "Give 99 bombs";
    case OP_LIVES: return "Set lives";
    case OP_SOLDIER: return "Soldier";
    case OP_FORM: return "Form";
    case OP_VEHICLE: return "Vehicle";
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
    case OP_RESUME: return "Continue";
    case OP_ARCADE: return "Arcade";
    case OP_MISSIONS: return "Mission";
    case OP_BOSS_RUSH: return "Boss Rush";
    case OP_RES_SCORE: return "Score";
    case OP_RES_TIME: return "Time";
    case OP_RES_CONT: return "Continues used";
    case OP_RES_BEST: return "Best score";
    case OP_RECORDS: return "Records";
    case OP_REC_ARCADE: return "Arcade: best score";
    case OP_REC_FURTHEST: return "Arcade: furthest mission";
    case OP_REC_RUSH: return "Boss Rush: best time";
    case OP_REC_RUSH_BOSSES: return "Boss Rush: most bosses";
    case OP_RES_BOSSES: return "Bosses defeated";
    case OP_RES_RUSH_BEST: return "Best time";
    case OP_RES_DONE: return "Back to start screen";
    case OP_INTEGER: return "Integer scale";
    case OP_WINDOW_SIZE: return "Window size";
    case OP_FOCUS_PAUSE: return "Pause when inactive";
    case OP_FAST_BOOT: return "Fast boot";
    case OP_BOSS_BAR: return "Boss health bar";
    case OP_RUNAHEAD: return "Run-ahead";
    case OP_PAD_CURSOR: return "Gamepad hides the cursor";
    case OP_RESET: return "Reset the machine";
    default: return "?";
    }
}

static const char *label(int op)
{
    if (op >= OP_BIND_ACT && op <= OP_BIND_ACT_LAST) return T(ACTION_LABEL[op - OP_BIND_ACT]);
    if (op >= OP_BIND_HK && op <= OP_BIND_HK_LAST) return T(HOTKEY_LABEL[op - OP_BIND_HK]);
    if ((op >= OP_MISSION_1 && op <= OP_MISSION_LAST) || (op >= OP_REC_M1 && op <= OP_REC_LAST)) {
        static char b[32];
        snprintf(b, sizeof b, T("Mission %d"), op - (op >= OP_REC_M1 && op <= OP_REC_LAST ? OP_REC_M1 : OP_MISSION_1) + 1);
        return b;
    }
    return T(label_en(op));
}

/* Debug dips behind each switch (the game profile's; 0 = the game has none). */
static uint16_t dip_of(int op)
{
    const ProfileDips *d = &game_profile.dips;
    switch (op) {
    case OP_INVINCIBLE: return d->invincible;
    case OP_INF_BOMBS: return d->infinite_bombs;
    case OP_INF_AMMO: return d->infinite_ammo;
    case OP_D_KILLS: return d->d_kills_all;
    case OP_BOXES: return d->boxes;
    case OP_INFO: return d->info;
    case OP_GROUND: return d->ground;
    case OP_CPU_METER: return d->cpu_meter;
    case OP_PA_POINT: return d->pa_point;
    case OP_PAUSE_ANY: return d->pause_anytime;
    default: return 0;
    }
}

static int submenu(int op)
{
    return op == OP_SETTINGS || op == OP_EXTRAS || op == OP_CHEATS || op == OP_DEBUG_PAGE || op == OP_MODES ||
           op == OP_SAVES || op == OP_SOUNDS || op == OP_RECORDS;
}

/* The controls tabs: section titles (not selectable) and the rows with a key or button. */
static int is_header(int op) { return op == OP_BIND_HEAD_GAME || op == OP_BIND_HEAD_HK; }
static int is_bind(int op)
{
    return (op >= OP_BIND_ACT && op <= OP_BIND_ACT_LAST) || (op >= OP_BIND_HK && op <= OP_BIND_HK_LAST);
}
static int on_gamepad_tab(void) { return m.level >= 0 && m.stack[m.level] == &P_GAMEPAD; }

/* A gamepad input's name as the connected gamepad prints it (Xbox, PlayStation or Nintendo
   layout); NULL if unbound. */
/* The gamepad type the names and the picture follow: the chosen style, or the connected one. */
static SDL_GamepadType pad_type(void)
{
    switch (cfg.pad_style[bind_player]) {
    case PAD_STYLE_XBOX: case PAD_STYLE_ARCADE: return SDL_GAMEPAD_TYPE_XBOXONE;    /* XInput sticks */
    case PAD_STYLE_PLAYSTATION: return SDL_GAMEPAD_TYPE_PS4;
    case PAD_STYLE_NINTENDO: return SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO;
    default: {
        SDL_Gamepad *g = fe_gamepad(bind_player);
        return g ? SDL_GetGamepadType(g) : SDL_GAMEPAD_TYPE_XBOXONE;
    }
    }
}

static const char *pad_label(int in)
{
    SDL_GamepadType t = pad_type();
    int ps = t == SDL_GAMEPAD_TYPE_PS3 || t == SDL_GAMEPAD_TYPE_PS4 || t == SDL_GAMEPAD_TYPE_PS5;
    int nin = t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR ||
              t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT || t == SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT;
    switch (in) {
    case PAD_NONE: return NULL;
    case PAD_LT: return ps ? "L2" : nin ? "ZL" : "LT";
    case PAD_RT: return ps ? "R2" : nin ? "ZR" : "RT";
    case SDL_GAMEPAD_BUTTON_SOUTH: case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_WEST: case SDL_GAMEPAD_BUTTON_NORTH:
        switch (SDL_GetGamepadButtonLabelForType(t, (SDL_GamepadButton)in)) {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return T("Cross");
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return T("Circle");
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return T("Square");
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return T("Triangle");
        default: return "?";
        }
    case SDL_GAMEPAD_BUTTON_BACK: return ps ? (t == SDL_GAMEPAD_TYPE_PS5 ? "Create" : t == SDL_GAMEPAD_TYPE_PS4 ? "Share" : "Select") : nin ? "-" : "Back";
    case SDL_GAMEPAD_BUTTON_START: return ps ? (t == SDL_GAMEPAD_TYPE_PS3 ? "Start" : "Options") : nin ? "+" : "Start";
    case SDL_GAMEPAD_BUTTON_GUIDE: return ps ? "PS" : nin ? "Home" : "Guide";
    case SDL_GAMEPAD_BUTTON_LEFT_STICK: return ps ? "L3" : "LS";
    case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return ps ? "R3" : "RS";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return ps ? "L1" : nin ? "L" : "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return ps ? "R1" : nin ? "R" : "RB";
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return T("D-pad up");
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return T("D-pad down");
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return T("D-pad left");
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return T("D-pad right");
    case SDL_GAMEPAD_BUTTON_MISC1: return ps ? "Mute" : nin ? "Capture" : "Share";
    case SDL_GAMEPAD_BUTTON_TOUCHPAD: return "Touchpad";
    case PAD_LS_UP: return T("Left stick up");
    case PAD_LS_DOWN: return T("Left stick down");
    case PAD_LS_LEFT: return T("Left stick left");
    case PAD_LS_RIGHT: return T("Left stick right");
    case PAD_RS_UP: return T("Right stick up");
    case PAD_RS_DOWN: return T("Right stick down");
    case PAD_RS_LEFT: return T("Right stick left");
    case PAD_RS_RIGHT: return T("Right stick right");
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1: return "P1";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE1: return "P2";
    case SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2: return "P3";
    case SDL_GAMEPAD_BUTTON_LEFT_PADDLE2: return "P4";
    default: return pad_input_name(in);
    }
}

/* Where a controls row's binding lives (the current tab's device; actions: the chosen player). */
static SDL_Scancode *key_slot(int op)
{
    return op >= OP_BIND_HK ? &cfg.hk_key[op - OP_BIND_HK] : &cfg.key[bind_player][op - OP_BIND_ACT];
}
static int *pad_slot(int op)
{
    return op >= OP_BIND_HK ? &cfg.hk_pad[op - OP_BIND_HK] : &cfg.button[bind_player][op - OP_BIND_ACT];
}

/* A row's binding as text; NULL if unbound. */
static const char *bind_text(int op)
{
    if (on_gamepad_tab()) return pad_label(*pad_slot(op));
    SDL_Scancode sc = *key_slot(op);
    return sc != SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeName(sc) : NULL;
}

static void bind_clear(int op)
{
    if (on_gamepad_tab()) { *pad_slot(op) = PAD_NONE; config_profile_store(&cfg, bind_player); return; }
    if (op == OP_BIND_HK + HK_MENU) { fe_notify(T("The menu needs a key")); return; }
    *key_slot(op) = SDL_SCANCODE_UNKNOWN;
}

/* The name of whatever else uses a binding, for the notice ("Jump (player 2)"). */
static const char *other_name(int is_hk, int j, int a)
{
    static char b[96];
    if (is_hk) return T(HOTKEY_LABEL[a]);
    if (j == bind_player) return T(ACTION_LABEL[a]);
    snprintf(b, sizeof b, T("%s (player %d)"), T(ACTION_LABEL[a]), j + 1);
    return b;
}

/* The new key for the row being changed. A key used elsewhere (the other player's actions too:
   one keyboard) swaps with the row's old one; the menu's key is never left unbound. */
static void bind_key(int op, SDL_Scancode sc)
{
    SDL_Scancode *slot = key_slot(op), old = *slot, *other = NULL;
    const char *who = NULL;
    for (int j = 0; j < 2 && !other; j++)
        for (int a = 0; a < ACT_COUNT && !other; a++)
            if (&cfg.key[j][a] != slot && cfg.key[j][a] == sc) { other = &cfg.key[j][a]; who = other_name(0, j, a); }
    for (int h = 0; h < HK_COUNT && !other; h++)
        if (&cfg.hk_key[h] != slot && cfg.hk_key[h] == sc) { other = &cfg.hk_key[h]; who = other_name(1, 0, h); }
    if (other == &cfg.hk_key[HK_MENU] && old == SDL_SCANCODE_UNKNOWN) { fe_notify(T("The menu needs a key")); return; }
    if (other) { *other = old; fe_notify(T("Swapped with %s"), who); }
    *slot = sc;
}

/* The same for a gamepad input: the chosen player's actions and the shortcuts (every gamepad). */
static void bind_pad(int op, int in)
{
    int *slot = pad_slot(op), old = *slot, *other = NULL;
    const char *who = NULL;
    for (int a = 0; a < ACT_COUNT && !other; a++)
        if (&cfg.button[bind_player][a] != slot && cfg.button[bind_player][a] == in) {
            other = &cfg.button[bind_player][a];
            who = other_name(0, bind_player, a);
        }
    for (int h = 0; h < HK_COUNT && !other; h++)
        if (&cfg.hk_pad[h] != slot && cfg.hk_pad[h] == in) { other = &cfg.hk_pad[h]; who = other_name(1, 0, h); }
    if (other) { *other = old; fe_notify(T("Swapped with %s"), who); }
    *slot = in;
    config_profile_store(&cfg, bind_player);
}

/* The current tab's defaults, for both players and the shortcuts. */
static void bind_defaults(void)
{
    Config d;
    config_defaults(&d);
    if (on_gamepad_tab()) {
        memcpy(cfg.button[bind_player], d.button[bind_player], sizeof cfg.button[0]);   /* this player's profile */
        cfg.pad_style[bind_player] = PAD_STYLE_AUTO;
        config_profile_store(&cfg, bind_player);
        memcpy(cfg.hk_pad, d.hk_pad, sizeof cfg.hk_pad);
    } else {
        memcpy(cfg.key, d.key, sizeof cfg.key);
        memcpy(cfg.hk_key, d.hk_key, sizeof cfg.hk_key);
    }
    fe_notify(T("Default controls restored"));
}

/* Values only shown, not changed (results and records). */
static int is_result(int op)
{
    return (op >= OP_RES_SCORE && op <= OP_RES_RUSH_BEST) || (op >= OP_REC_RUSH && op <= OP_REC_LAST);
}

static void time_text(uint64_t frames, char *b, size_t n)
{
    unsigned s = (unsigned)(frames * 10000 / 591856);       /* 59.1856 frames per second */
    snprintf(b, n, "%u:%02u:%02u", s / 3600, s / 60 % 60, s % 60);
}

/* Value on the right (empty if the option has no value). */
static void value(int op, char *b, size_t n)
{
    static const char *const ASPECT[ASPECT_COUNT] = {"4:3", "Square"};
    static const char *const FILTER[FILTER_COUNT] = {"Sharp", "Smooth", "Pixel"};
    static const char *const AUTO[AUTO_COUNT] = {"Off", "Own button", "On fire"};
    static const char *const SPEED[AUTO_SPEEDS] = {"10/s", "7.5/s", "5/s"};
    static const char *const LANGUAGE[LANG_COUNT] = {"English", "Español"};   /* never translated */
    b[0] = 0;
    switch (op) {
    case OP_SAVE: case OP_LOAD: snprintf(b, n, T("Slot %d"), cfg.slot); break;
    case OP_LANGUAGE: snprintf(b, n, "%s", LANGUAGE[cfg.language]); break;
    case OP_FULLSCREEN: snprintf(b, n, "%s", T(cfg.fullscreen ? "Yes" : "No")); break;
    case OP_VSYNC: snprintf(b, n, "%s", T(cfg.vsync ? "Yes" : "No")); break;
    case OP_ASPECT: snprintf(b, n, "%s", T(ASPECT[cfg.aspect])); break;
    case OP_FILTER: snprintf(b, n, "%s", T(FILTER[cfg.filter])); break;
    case OP_SCANLINES: snprintf(b, n, "%s", T(cfg.scanlines ? "Yes" : "No")); break;
    case OP_CRT_CURVE: snprintf(b, n, "%s", T(cfg.crt_curve ? "Yes" : "No")); break;
    case OP_CRT: {
        static const char *const CRT[CRT_COUNT] = {"Off", "Arcade", "TV", "Soft"};
        snprintf(b, n, "%s", T(CRT[cfg.crt]));
        break;
    }
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
    case OP_INTEGER: snprintf(b, n, "%s", T(cfg.integer_scale ? "Yes" : "No")); break;
    case OP_FOCUS_PAUSE: snprintf(b, n, "%s", T(cfg.focus_pause ? "Yes" : "No")); break;
    case OP_PAD_CURSOR: snprintf(b, n, "%s", T(cfg.pad_hides_cursor ? "Yes" : "No")); break;
    case OP_FAST_BOOT: snprintf(b, n, "%s", T(cfg.fast_boot ? "Yes" : "No")); break;
    case OP_BOSS_BAR: snprintf(b, n, "%s", T(cfg.boss_bar ? "Yes" : "No")); break;
    case OP_RUNAHEAD:
        if (cfg.runahead) snprintf(b, n, T(cfg.runahead == 1 ? "%d frame" : "%d frames"), cfg.runahead);
        else snprintf(b, n, "%s", T("Off"));
        break;
    case OP_WINDOW_SIZE: snprintf(b, n, "x%d", cfg.scale); break;
    case OP_ARCADE:
        if (ARC_CONTINUES[arc_choice] < 0) snprintf(b, n, "%s", T("Infinite"));
        else snprintf(b, n, "%d", ARC_CONTINUES[arc_choice]);
        break;
    case OP_RES_SCORE: case OP_RES_TIME: case OP_RES_CONT: {
        uint32_t score;
        uint64_t frames;
        int used;
        fe_arcade_result(&score, &frames, &used);
        if (op == OP_RES_SCORE) snprintf(b, n, "%u", (unsigned)score);
        else if (op == OP_RES_TIME && fe_rush_mode()) records_time_text((uint32_t)frames, b, (int)n);
        else if (op == OP_RES_TIME) time_text(frames, b, n);
        else snprintf(b, n, "%d", used);
        break;
    }
    case OP_RES_BEST: case OP_REC_ARCADE:
        if (records_arcade_score()) snprintf(b, n, "%u", (unsigned)records_arcade_score());
        else snprintf(b, n, "-");
        break;
    case OP_REC_FURTHEST:
        if (records_arcade_mission()) snprintf(b, n, T("Mission %d"), records_arcade_mission());
        else snprintf(b, n, "-");
        break;
    case OP_RES_BOSSES: {
        int d, t, c;
        fe_rush_result(&d, &t, &c);
        snprintf(b, n, "%d / %d%s%s", d, t, c ? "  ·  " : "", c ? T("Cleared!") : "");
        break;
    }
    case OP_RES_RUSH_BEST: case OP_REC_RUSH:
        if (records_rush_frames()) records_time_text(records_rush_frames(), b, (int)n);
        else snprintf(b, n, "-");
        break;
    case OP_REC_RUSH_BOSSES:
        if (records_rush_defeated()) snprintf(b, n, "%d / %d", records_rush_defeated(), bossrush_total());
        else snprintf(b, n, "-");
        break;
    case OP_START_MISSION: {
        char mission[32];
        snprintf(mission, sizeof mission, T("Mission %d"), cheats_start_mission + 1);
        snprintf(b, n, "%s", cheats_start_mission ? mission : T("Normal"));
        break;
    }
    case OP_CHEAT_PLAYER: snprintf(b, n, "%d", cheat_player + 1); break;
    case OP_GIVE_WEAPON:
        if (cheat_weapon >= cheats_weapons()) cheat_weapon = cheats_weapons() - 1;   /* fewer weapons (Metal Slug) */
        snprintf(b, n, "%s", T(cheats_weapon_name(cheat_weapon)));
        break;
    case OP_LIVES: snprintf(b, n, "%d", cheat_lives); break;
    case OP_VEHICLE: snprintf(b, n, "%s", T(game_profile.vehicles[cheat_vehicle].name)); break;
    case OP_SOLDIER: snprintf(b, n, "%s", cheats_soldier_name(cheats_soldier(cheat_player))); break;
    case OP_FORM: snprintf(b, n, "%s", T(game_profile.forms[form_index()].name)); break;
    case OP_INVINCIBLE: case OP_INF_BOMBS: case OP_INF_AMMO: case OP_D_KILLS:
    case OP_BOXES: case OP_INFO: case OP_GROUND: case OP_CPU_METER: case OP_PA_POINT: case OP_PAUSE_ANY:
        snprintf(b, n, "%s", T((cheats_dips & dip_of(op)) == dip_of(op) ? "Yes" : "No"));
        break;
    case OP_DEVMODE: snprintf(b, n, "%s", T(cfg.devmode ? "Yes" : "No")); break;
    case OP_BIND_PLAYER: snprintf(b, n, "%d", bind_player + 1); break;
    case OP_BIND_STYLE: case OP_BIND_PROFILE: {
        static const char *const STYLE[PAD_STYLE_COUNT] = {"Automatic", "Xbox / XInput", "PlayStation", "Nintendo",
                                                           "Arcade stick"};
        if (op == OP_BIND_STYLE) snprintf(b, n, "%s", T(STYLE[cfg.pad_style[bind_player]]));
        else snprintf(b, n, "%d  ·  %s", cfg.pad_profile[bind_player] + 1, T(STYLE[cfg.pad_style[bind_player]]));
        break;
    }
    case OP_BIND_DEVICE: {
        SDL_Gamepad *g = fe_player_gamepad(bind_player);
        const char *name = g ? SDL_GetGamepadName(g) : NULL;
        snprintf(b, n, "%.30s", name ? name : T("None"));
        break;
    }
    default:
        if (op >= OP_REC_M1 && op <= OP_REC_LAST) {
            int k = op - OP_REC_M1 + 1;
            if (!records_mission_has(k)) { snprintf(b, n, "-"); break; }
            char t[32];
            records_time_text(records_mission_frames(k), t, sizeof t);
            snprintf(b, n, "%s  ·  %u", t, (unsigned)records_mission_score(k));
        }
        break;
    }
}

static const char *help_en(int op)
{
    switch (op) {
    case OP_LANGUAGE: return "Language / Idioma";            /* both: findable in either language */
    case OP_PLAY: return "A new game from the beginning";
    case OP_CONTINUE: return "Back to the game";
    case OP_MODES: return "Arcade, Mission mode and more";
    case OP_LOAD: return "Load a saved game; </> choose the slot";
    case OP_SAVES: return "Save or load the game in one of nine slots";
    case OP_SAVE: return "Saves the game in this slot; </> choose the slot";
    case OP_SETTINGS: return "Video, audio, game, controls and system";
    case OP_EXTRAS: return "Music and sounds, cheats, debug, service menu";
    case OP_BACK_TO_START: return "Ends this game and goes back to the start screen";
    case OP_QUIT: return "Closes the game";
    case OP_TURBO: return "Original: slows down with many sprites, like the arcade";
    case OP_ASPECT: return "4:3 like the original monitor, or square pixels";
    case OP_VSYNC: return "No tearing; at 60 Hz the game runs 1.4% faster";
    case OP_BLOOD: return "Original: as set in the service menu (BLOOD, off in the US)";
    case OP_FILTER: return "Sharp: even pixels without blurry edges. Smooth: a softer image. Pixel: no filter";
    case OP_SCANLINES: return "Dark lines between the rows, like a CRT monitor";
    case OP_CRT_CURVE: return "The tube's curved glass: the edges bow out a little (more on the TV style)";
    case OP_CRT:
        return "Like a tube screen. Arcade: an arcade monitor (RGB stripes). TV: a home TV (slot mask). "
               "Soft: scanlines and a little glow only";
    case OP_FULLSCREEN: return "Also with F11 or Alt+Enter";
    case OP_VOLUME: return "Music and effects";
    case OP_RUMBLE: return "Gamepad vibration with explosions and hits";
    case OP_AUTO1: case OP_AUTO2: return "Own button: B / G on the keyboard, RB on the gamepad";
    case OP_AUTO_SPEED: return "Shots per second of the autofire";
    case OP_BIND_PLAYER: return "The game controls of player 1 or 2; the shortcuts are shared by both";
    case OP_BIND_DEVICE: return "Which of the connected gamepads this player uses; remembered for next time";
    case OP_BIND_PROFILE:
        return "Four gamepad setups, each with its buttons and type. The one you choose with a gamepad connected "
               "loads by itself when that gamepad connects again";
    case OP_BIND_STYLE:
        return "The buttons' names and the picture: as detected, or as your gamepad prints them. Arcade stick: "
               "a lever and eight buttons (XInput sticks such as Mayflash's)";
    case OP_BIND_RESET:
        return on_gamepad_tab() ? "The default buttons of this player's profile and of the shortcuts"
                                : "The default keys of this tab, for both players";
    case OP_SERVICE: return "BIOS menu: difficulty, lives, time (also with F2)";
    case OP_SOUNDS: return "Listen to the music, voices and effects of the game";
    case OP_DEBUG: return "68000 load, slowdown, sprites, palette, RAM";
    case OP_CHEATS: return "Weapons, bombs, lives, invincibility";
    case OP_DEBUG_PAGE: return "The developers' own debug switches and hidden menu";
    case OP_RESET: return "Like switching the machine off and on";
    case OP_GIVE_WEAPON:
        return game_profile.item_type ? "</>: choose, Enter/A: its item appears in front of you"
                                      : "</>: choose, Enter/A: give it (during a mission)";
    case OP_VEHICLE: return "</>: choose, Enter/A: it appears in front of you";
    case OP_SOLDIER: return "Takes effect when the player respawns";
    case OP_START_MISSION: return "New games start at this mission";
    case OP_FORM: return game_profile.form_help;
    case OP_D_KILLS: return "Press D in game: every enemy on screen dies";
    case OP_DEVMODE: return "The game believes it runs on SNK's development hardware";
    case OP_DEV_MENU: return "Stage select, test stages, sound test, debug dips";
    case OP_PA_POINT: return "Marks a reference point used by the developers";
    case OP_CPU_METER: return "The screen darkens while the 68000 is busy";
    case OP_START_SCREEN: return "Show this menu when the game opens";
    case OP_RESUME: return "Back to the game you left when you quit";
    case OP_ARCADE: return "The whole game like the original machine. No saving or cheats.";
    case OP_MISSIONS: return "Start at any mission you have already reached.";
    case OP_BOSS_RUSH: return "Every boss in a row in one game, with one credit.";
    case OP_INTEGER: return "Sharpest pixels at any resolution (4K, 8K): black borders may appear";
    case OP_WINDOW_SIZE: return "Up to the largest that fits your monitor";
    case OP_FOCUS_PAUSE: return "The game pauses when you switch to another window";
    case OP_PAD_CURSOR:
        return "Using the gamepad hides the mouse cursor, and the mouse stops moving the selection, until you "
               "move the mouse or press a key";
    case OP_FAST_BOOT: return "Skip the green boot screen: the boot runs unseen in an instant";
    case OP_BOSS_BAR: return "The life of the bosses, from their first hit (drawn over the game, the game is not changed)";
    case OP_RUNAHEAD:
        return "Less input lag: the game's reaction is shown sooner by emulating ahead (the game is not "
               "changed). 1 frame is enough for Metal Slug 3; 2 may make the image jump";
    case OP_RES_DONE: return "Back to the start screen";
    case OP_RECORDS: return "Best times and scores of each mission and of Arcade mode";
    case OP_RES_BOSSES: return "In one game: two in Mission 2, two in Mission 5";
    case OP_RES_BEST: case OP_REC_ARCADE: case OP_REC_FURTHEST: case OP_REC_RUSH: case OP_REC_RUSH_BOSSES:
    case OP_RES_RUSH_BEST:
        return "Only clean games count: no cheats, debug options, slowdown off or loaded games";
    default: return "";
    }
}

static const char *help(int op)
{
    if (is_bind(op))
        return T(on_gamepad_tab() ? "A: press the new button (wait 6 s to cancel). Y: clear. The triggers count as buttons"
                                  : "Enter: press the new key (Esc cancels). Del: clear");
    if (op >= OP_REC_M1 && op <= OP_REC_LAST) op = OP_REC_ARCADE;     /* the same note */
    /* with the slowdown off, the mission records say so (they are not being saved) */
    if ((op == OP_RECORDS || (op >= OP_REC_RUSH && op <= OP_REC_LAST)) && cpu_turbo)
        return T("The slowdown is off (Settings > Game): mission and Boss Rush records are not saved until it is back to the original");
    return T(help_en(op));
}

/* ------------------------------------------------------------------ available options */
/* Options that need addresses the game profile does not have are not shown (Metal Slug 3's
   profile grows as its addresses are verified: docs/PHASE4_PROFILE.md). */
static int page_ops(const Page *p, int out[32]);

static int available(int op)
{
    const GameProfile *g = &game_profile;
    switch (op) {
    case OP_START_MISSION: return g->missions > 1;
    case OP_CHEAT_PLAYER: case OP_MAX_BOMBS: case OP_LIVES: return g->player_table != 0;
    case OP_SOLDIER: return g->player_table != 0 && g->player1_soldier != 0;   /* Metal Slug: no soldier select */
    case OP_GIVE_WEAPON: return g->weapons > 0;
    case OP_VEHICLE: return g->n_vehicles > 0;
    case OP_FORM: return g->n_forms > 1;
    case OP_INVINCIBLE: case OP_INF_BOMBS: case OP_INF_AMMO: case OP_D_KILLS:
    case OP_BOXES: case OP_INFO: case OP_GROUND: case OP_CPU_METER: case OP_PA_POINT: case OP_PAUSE_ANY:
        return profile_supported() && g->dips_word != 0 && dip_of(op) != 0;
    case OP_DEVMODE: return profile_supported() && g->devmode_word != 0;
    case OP_DEV_MENU: return profile_supported() && g->devmode_word != 0 && g->dev_menu;
    case OP_BLOOD: return g->blood_word != 0;
    case OP_CHEATS: { int o[32]; return !fe_arcade_active() && page_ops(&P_CHEATS, o) > 0; }
    case OP_TURBO: return !fe_arcade_active() || fe_rush_mode();       /* Boss Rush keeps it */
    case OP_SAVES: case OP_SAVE: case OP_LOAD: case OP_DEBUG: case OP_SERVICE: case OP_RESET:
    case OP_DEBUG_PAGE:
        return !fe_arcade_active();
    case OP_RESUME: return fe_resume_available();
    case OP_MISSIONS: return progress_supported();
    case OP_WINDOW_SIZE: return !cfg.fullscreen;
    case OP_BIND_DEVICE: case OP_BIND_STYLE: case OP_BIND_PROFILE: return on_gamepad_tab();
    case OP_FILTER: case OP_SCANLINES: return !cfg.crt;        /* the CRT effect sets its own */
    case OP_CRT_CURVE: return cfg.crt != 0;
    case OP_BOSS_BAR: return bossbar_supported();
    case OP_RECORDS: case OP_RES_BEST: case OP_REC_ARCADE: case OP_REC_FURTHEST: return records_supported();
    case OP_BOSS_RUSH: case OP_REC_RUSH: case OP_REC_RUSH_BOSSES: case OP_RES_RUSH_BEST: return bossrush_supported();
    default:
        if (op >= OP_MISSION_1 && op <= OP_MISSION_LAST) return op - OP_MISSION_1 < (int)g->missions;
        if (op >= OP_REC_M1 && op <= OP_REC_LAST) return records_supported() && op - OP_REC_M1 < (int)g->missions;
        return 1;
    }
}

static int page_ops(const Page *p, int out[32])
{
    int n = 0;
    for (int i = 0; i < p->n && n < 32; i++)
        if (available(p->op[i])) out[n++] = p->op[i];
    return n;
}

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
    /* a cheat used during a mission: that mission does not count for the records */
    if ((!d && (op == OP_GIVE_WEAPON || op == OP_VEHICLE || op == OP_MAX_BOMBS || op == OP_LIVES || op == OP_DEV_MENU)) ||
        op == OP_SOLDIER || op == OP_FORM)
        records_invalidate();
    switch (op) {
    case OP_RECORDS: if (!d) push_page(&P_RECORDS); break;
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
    case OP_SETTINGS: if (!d) push_page(SETTINGS_TABS[last_tab]); break;
    case OP_SOUNDS: if (!d) sounds_show(); break;
    case OP_EXTRAS: if (!d) push_page(&P_EXTRAS); break;
    case OP_MODES: if (!d) push_page(&P_MODES); break;
    case OP_SAVES: if (!d) push_page(&P_SAVES); break;
    case OP_BACK_TO_START: case OP_RES_DONE: if (!d) { fe_back_to_start(); menu_start(); } break;
    case OP_RESUME: if (!d && fe_resume()) close_menu(); break;
    case OP_ARCADE:
        if (d) arc_choice = cycle(arc_choice, d, N_ARC_CONTINUES);
        else { close_menu(); fe_arcade_start(ARC_CONTINUES[arc_choice]); }
        break;
    case OP_MISSIONS: if (!d) push_page(&P_MISSIONS); break;
    case OP_BOSS_RUSH:
        if (d) break;
        if (!progress_rush_unlocked()) fe_notify(T("Complete the game in Arcade or Mission mode first"));
        else { close_menu(); fe_rush_start(); }
        break;
    case OP_INTEGER: cfg.integer_scale = !cfg.integer_scale; break;
    case OP_FOCUS_PAUSE: cfg.focus_pause = !cfg.focus_pause; break;
    case OP_PAD_CURSOR: cfg.pad_hides_cursor = !cfg.pad_hides_cursor; break;
    case OP_FAST_BOOT: cfg.fast_boot = !cfg.fast_boot; break;
    case OP_BOSS_BAR: cfg.boss_bar = !cfg.boss_bar; break;
    case OP_RUNAHEAD: cfg.runahead = cycle(cfg.runahead, d ? d : 1, 3); break;
    case OP_WINDOW_SIZE: {
        int max = fe_max_window_scale();
        cfg.scale = d ? cycle(cfg.scale - 1, d, max) + 1 : cfg.scale % max + 1;
        fe_window_size(cfg.scale);
        break;
    }
    case OP_QUIT: if (!d) fe_quit(); break;
    case OP_LANGUAGE: cfg.language = cycle(cfg.language, d ? d : 1, LANG_COUNT); lang_set(cfg.language); break;
    case OP_FULLSCREEN: cfg.fullscreen = !cfg.fullscreen; fe_apply_video(); break;
    case OP_VSYNC: cfg.vsync = !cfg.vsync; fe_apply_video(); break;
    case OP_ASPECT: cfg.aspect = cycle(cfg.aspect, d ? d : 1, ASPECT_COUNT); break;
    case OP_FILTER: cfg.filter = cycle(cfg.filter, d ? d : 1, FILTER_COUNT); break;
    case OP_SCANLINES: cfg.scanlines = !cfg.scanlines; break;
    case OP_CRT: cfg.crt = cycle(cfg.crt, d ? d : 1, CRT_COUNT); break;
    case OP_CRT_CURVE: cfg.crt_curve = !cfg.crt_curve; break;
    case OP_VOLUME: step_percent(&cfg.volume, d); fe_apply_volume(); break;
    case OP_RUMBLE: step_percent(&cfg.rumble, d); rumble_strength(cfg.rumble); break;
    case OP_TURBO: cfg.turbo = cycle(cfg.turbo, d ? d : 1, 3); cpu_turbo = cfg.turbo; break;
    case OP_BLOOD:
        if (!profile_supported()) fe_notify(T("Unrecognized game version: cheats and debug are off"));
        else { cfg.blood = !cfg.blood; fe_apply_blood(); }
        break;
    case OP_AUTO1: case OP_AUTO2:
        cfg.autofire[op - OP_AUTO1] = cycle(cfg.autofire[op - OP_AUTO1], d ? d : 1, AUTO_COUNT);
        break;
    case OP_AUTO_SPEED: cfg.auto_speed = cycle(cfg.auto_speed, d ? d : 1, AUTO_SPEEDS); break;
    case OP_START_SCREEN: cfg.start_screen = !cfg.start_screen; break;
    case OP_BIND_PLAYER: bind_player = !bind_player; break;
    case OP_BIND_DEVICE: fe_cycle_gamepad(bind_player, d ? d : 1); break;
    case OP_BIND_STYLE:
        cfg.pad_style[bind_player] = cycle(cfg.pad_style[bind_player], d ? d : 1, PAD_STYLE_COUNT);
        config_profile_store(&cfg, bind_player);
        break;
    case OP_BIND_PROFILE: {
        int p = bind_player;
        cfg.pad_profile[p] = cycle(cfg.pad_profile[p], d ? d : 1, PAD_PROFILES);
        config_profile_load(&cfg, p);
        SDL_Gamepad *g = fe_player_gamepad(p);
        if (g) {                                         /* linked to this gamepad, and only this profile */
            char guid[40];
            SDL_GUIDToString(SDL_GetGamepadGUIDForID(SDL_GetGamepadID(g)), guid, sizeof guid);
            for (int k = 0; k < PAD_PROFILES; k++)
                if (!strcmp(cfg.profile[k].guid, guid)) cfg.profile[k].guid[0] = 0;
            snprintf(cfg.profile[cfg.pad_profile[p]].guid, sizeof cfg.profile[0].guid, "%s", guid);
        }
        break;
    }
    case OP_BIND_RESET: {                              /* a second press within 3 s confirms */
        static Uint64 armed;
        if (d) break;
        if (SDL_GetTicks() < armed) { bind_defaults(); armed = 0; }
        else { armed = SDL_GetTicks() + 3000; fe_notify(T("Press again to restore the defaults")); }
        break;
    }
    case OP_DEBUG: debug_enable(!debug_enabled()); cfg.debug = debug_enabled(); break;
    case OP_CHEATS:
        if (d) break;
        if (!profile_supported()) fe_notify(T("Unrecognized game version: cheats and debug are off"));
        else push_page(&P_CHEATS);
        break;
    case OP_DEBUG_PAGE: if (!d) push_page(&P_DEBUG); break;
    case OP_START_MISSION: cheats_start_mission = cycle(cheats_start_mission, d ? d : 1, (int)game_profile.missions); break;
    case OP_CHEAT_PLAYER: cheat_player = !cheat_player; break;
    case OP_GIVE_WEAPON:
        if (d) cheat_weapon = cycle(cheat_weapon, d, cheats_weapons());
        else if (cheats_give_weapon(cheat_player, cheat_weapon)) close_menu();
        else fe_notify(T("Only during a mission"));
        break;
    case OP_VEHICLE:
        if (d) cheat_vehicle = cycle(cheat_vehicle, d, game_profile.n_vehicles);
        else if (cheats_spawn_vehicle(cheat_vehicle)) close_menu();
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
        int i = cycle(form_index(), d < 0 ? -1 : 1, game_profile.n_forms);
        if (cheats_set_form(cheat_player, i ? game_profile.forms[i].form : FORM_NORMAL)) close_menu();
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
    default:
        if (is_bind(op) && !d) {                          /* wait for the new key or button */
            m.capture = op;
            m.capture_until = SDL_GetTicks() + CAPTURE_MS;
        }
        if (op >= OP_MISSION_1 && op <= OP_MISSION_LAST && !d) {
            int k = op - OP_MISSION_1;
            if (k >= progress_unlocked()) fe_notify(T("Complete mission %d first"), k);
            else { close_menu(); fe_mission_start(k); }
        }
        break;
    }
}

void menu_navigate(int t)
{
    if (m.level < 0) return;
    const Page *p = m.stack[m.level];
    int op[32], n = page_ops(p, op);
    int *s = &m.sel[m.level];
    if (*s >= n) *s = 0;
    int tab = tab_of(p);
    if (t == 9 || t == 10) {                      /* LB / RB: the settings tabs, else left / right */
        if (tab < 0) { t = t == 9 ? 2 : 3; }
        else {
            last_tab = cycle(tab, t == 9 ? -1 : 1, N_TABS);
            m.stack[m.level] = SETTINGS_TABS[last_tab];
            *s = 0;
            return;
        }
    }
    if (!n && t != 5) return;
    if (p->layout == L_MODES || p->layout == L_MISSIONS) {
        /* cards side by side: left / right moves, up / down changes the card's value */
        switch (t) {
        case 2: *s = cycle(*s, -1, n); break;
        case 3: *s = cycle(*s, 1, n); break;
        case 0: do_action(op[*s], 1); break;
        case 1: do_action(op[*s], -1); break;
        case 4: do_action(op[*s], 0); break;
        case 5: go_back(); break;
        default: break;
        }
        return;
    }
    switch (t) {
    case 0: do *s = cycle(*s, -1, n); while (is_header(op[*s])); break;
    case 1: do *s = cycle(*s, 1, n); while (is_header(op[*s])); break;
    case 2: do_action(op[*s], -1); break;
    case 3: do_action(op[*s], 1); break;
    case 4: do_action(op[*s], 0); break;
    case 5: go_back(); break;
    case 6: if (is_bind(op[*s])) bind_clear(op[*s]); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ controls */
int menu_capturing(void) { return m.capture >= 0; }

static void capture_done(void)
{
    m.capture = -1;
    fe_save_config();
}

void menu_capture_key(SDL_Scancode sc)
{
    int op = m.capture;
    if (on_gamepad_tab()) {                                 /* the keyboard only cancels */
        if (sc == SDL_SCANCODE_ESCAPE) { fe_notify(T("Unchanged")); capture_done(); }
        return;
    }
    if (sc == SDL_SCANCODE_ESCAPE && op != OP_BIND_HK + HK_MENU) { fe_notify(T("Unchanged")); capture_done(); return; }
    if (sc == SDL_SCANCODE_DELETE || sc == SDL_SCANCODE_BACKSPACE) bind_clear(op);
    else bind_key(op, sc);
    capture_done();
}

void menu_capture_button(int in)
{
    if (!on_gamepad_tab()) return;                          /* the keyboard tab waits for a key */
    bind_pad(m.capture, in);
    capture_done();
}

/* ------------------------------------------------------------------ drawing: primitives */
/* The palette: gunmetal ground, bone text, amber accent. */
#define C_GROUND 0x0E1012u
#define C_PANEL 0x161A1Du
#define C_PANEL_DIM 0x121518u
#define C_INSET 0x101315u
#define C_LINE 0x2E353Cu
#define C_ROW_LINE 0x1E2327u
#define C_TEXT 0xECE6D8u
#define C_BODY 0xC9C3B6u
#define C_MUTED 0x9AA3A8u
#define C_DIM 0x8A9298u
#define C_ACCENT 0xF2A900u
#define C_BAR 0x0A0C0Du
#define C_DONE 0x9CCB6Bu

static SDL_Renderer *R;
static float VW, VH;                          /* the virtual canvas */
static float K = 1;                           /* output pixels per canvas unit */

/* Clickable areas of the last frame drawn (canvas units). */
enum { HIT_ITEM, HIT_ARROW, HIT_TAB, HIT_NAV };
typedef struct { SDL_FRect r; int kind, index, d; } Hit;
static Hit hits[96];
static int n_hits;

static void add_hit(float x, float y, float w, float h, int kind, int index, int d)
{
    if (n_hits < (int)(sizeof hits / sizeof hits[0])) hits[n_hits++] = (Hit){{x, y, w, h}, kind, index, d};
}

static SDL_Color col(uint32_t c, int a) { return (SDL_Color){(Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, (Uint8)a}; }

static void fill(float x, float y, float w, float h, uint32_t c, int a)
{
    SDL_SetRenderDrawColor(R, (Uint8)(c >> 16), (Uint8)(c >> 8), (Uint8)c, (Uint8)a);
    SDL_FRect f = {x, y, w, h};
    SDL_RenderFillRect(R, &f);
}

static void outline(float x, float y, float w, float h, float t, uint32_t c)
{
    fill(x, y, w, t, c, 255);
    fill(x, y + h - t, w, t, c, 255);
    fill(x, y + t, t, h - 2 * t, c, 255);
    fill(x + w - t, y + t, t, h - 2 * t, c, 255);
}

/* A convex polygon of 3 or 4 points. */
static void poly(const float *xy, int n, uint32_t c, int a)
{
    SDL_FColor fc = {(c >> 16 & 255) / 255.0f, (c >> 8 & 255) / 255.0f, (c & 255) / 255.0f, a / 255.0f};
    SDL_Vertex v[4];
    for (int i = 0; i < n; i++) v[i] = (SDL_Vertex){{xy[2 * i], xy[2 * i + 1]}, fc, {0, 0}};
    static const int idx[6] = {0, 1, 2, 0, 2, 3};
    SDL_RenderGeometry(R, NULL, v, n, idx, n == 4 ? 6 : 3);
}

/* Triangle pointing right (dir 0), up (1) or down (2), centred on (x, y). */
static void triangle(float x, float y, float s, int dir, uint32_t c)
{
    float h = s / 2;
    float p[6];
    if (dir == 0) { float q[6] = {x - h * 0.8f, y - h, x + h * 0.9f, y, x - h * 0.8f, y + h}; memcpy(p, q, sizeof p); }
    else if (dir == 1) { float q[6] = {x - h, y + h * 0.7f, x, y - h * 0.8f, x + h, y + h * 0.7f}; memcpy(p, q, sizeof p); }
    else { float q[6] = {x - h, y - h * 0.7f, x + h, y - h * 0.7f, x, y + h * 0.8f}; memcpy(p, q, sizeof p); }
    poly(p, 3, c, 255);
}

/* Warning stripes (135 degrees) in a vertical band. */
static void hazard(float x, float y0, float w, float y1)
{
    fill(x, y0, w, y1 - y0, C_GROUND, 255);
    for (float y = y0 - w; y < y1; y += 20) {
        float a = SDL_max(y, y0), b = SDL_min(y + 10, y1);
        if (b <= a) continue;
        /* the stripe at the left edge spans [y, y + 10]; it rises by w across the band */
        float p[8] = {x, a, x, b, x + w, SDL_max(b - w, y0), x + w, SDL_max(a - w, y0)};
        poly(p, 4, C_ACCENT, 255);
    }
}

static void lock_icon(float cx, float cy, float s, uint32_t c)
{
    float bw = s * 0.62f, bh = s * 0.46f, t = SDL_max(1.5f, s * 0.09f);
    fill(cx - bw / 2, cy - bh / 2 + s * 0.12f, bw, bh, c, 255);
    float sw = bw * 0.62f, sh = s * 0.36f;
    outline(cx - sw / 2, cy - bh / 2 + s * 0.12f - sh + t, sw, sh, t, c);
}

static float text(int font, float size, float sp, float x, float y, uint32_t c, const char *s)
{
    return ui_text(R, font, size, sp, x, y, col(c, 255), s);
}

static float text_r(int font, float size, float sp, float right, float y, uint32_t c, const char *s)
{
    return ui_text_right(R, font, size, sp, right, y, col(c, 255), s);
}

static float text_c(int font, float size, float sp, float cx, float y, uint32_t c, const char *s)
{
    return ui_text_centered(R, font, size, sp, cx, y, col(c, 255), s);
}

/* The size (at most `size`) at which the text fits in `w`. */
static float fit(int font, float size, float sp, const char *s, float w)
{
    float tw = ui_text_width(font, size, sp, s);
    return tw > w && tw > 0 ? size * w / tw : size;
}

static const char *up(const char *s)
{
    static char b[4][256];
    static int i;
    i = (i + 1) & 3;
    return font_upper(s, b[i], sizeof b[i]);
}

/* Text wrapped to `w`; returns the height used. */
static float wrap(int font, float size, float x, float y, float w, float lh, uint32_t c, const char *s)
{
    char line[256] = "";
    float top = y;
    while (*s) {
        while (*s == ' ') s++;
        const char *e = s;
        while (*e && *e != ' ') e++;
        char word[128];
        size_t n = (size_t)(e - s) < sizeof word - 1 ? (size_t)(e - s) : sizeof word - 1;
        memcpy(word, s, n);
        word[n] = 0;
        char trial[256];
        snprintf(trial, sizeof trial, "%s%s%s", line, line[0] ? " " : "", word);
        if (line[0] && ui_text_width(font, size, 0, trial) > w) {
            text(font, size, 0, x, y + lh / 2, c, line);
            y += lh;
            snprintf(line, sizeof line, "%s", word);
        } else {
            snprintf(line, sizeof line, "%s", trial);
        }
        s = e;
    }
    if (line[0]) { text(font, size, 0, x, y + lh / 2, c, line); y += lh; }
    return y - top;
}

/* ------------------------------------------------------------------ drawing: shared parts */
typedef struct { const char *pad, *key, *what; int nav; } Hint;   /* nav: menu_navigate() code */

/* A key or button in the hints: a circle for the face buttons, a key cap for the rest. */
static float keycap(float x, float y, const char *s)
{
    if (!strcmp(s, "^v")) {                       /* up / down */
        fill(x, y - 13, 34, 26, C_TEXT, 255);
        triangle(x + 11, y, 10, 1, C_GROUND);
        triangle(x + 23, y, 10, 2, C_GROUND);
        return 34;
    }
    float w = ui_text_width(UI_BOLD, 14, 0, s);
    if (strlen(s) == 1) {
        float rr = 13;
        SDL_FColor fc = {0xEC / 255.0f, 0xE6 / 255.0f, 0xD8 / 255.0f, 1};
        SDL_Vertex v[18];
        int idx[48];
        v[0] = (SDL_Vertex){{x + rr, y}, fc, {0, 0}};
        for (int i = 0; i < 16; i++) {
            float a = i * 6.2831853f / 16;
            v[i + 1] = (SDL_Vertex){{x + rr + SDL_cosf(a) * rr, y + SDL_sinf(a) * rr}, fc, {0, 0}};
            idx[3 * i] = 0;
            idx[3 * i + 1] = i + 1;
            idx[3 * i + 2] = (i + 1) % 16 + 1;
        }
        SDL_RenderGeometry(R, NULL, v, 17, idx, 48);
        text_c(UI_BOLD, 14, 0, x + rr, y, C_GROUND, s);
        return 2 * rr;
    }
    fill(x, y - 13, w + 16, 26, C_TEXT, 255);
    text(UI_BOLD, 14, 0, x + 8, y, C_GROUND, s);
    return w + 16;
}

static void footer(const Hint *h, int n)
{
    float y = VH - 56;
    fill(0, y, VW, 56, C_BAR, 255);
    fill(0, y, VW, 2, C_ACCENT, 255);
    float x = 40, mid = y + 29;
    for (int i = 0; i < n; i++) {
        float x0 = x;
        x += keycap(x, mid, gamepad_hints ? h[i].pad : h[i].key) + 8;
        x += text(UI_SEMIBOLD, 15, 0.5f, x, mid, C_TEXT, T(h[i].what));
        add_hit(x0 - 6, y + 4, x - x0 + 12, 50, HIT_NAV, h[i].nav, 0);
        x += 28;
    }
    char b[128];
    snprintf(b, sizeof b, "%s", up(game_profile.title ? game_profile.title : ""));
    text_r(UI_SEMIBOLD, 14, 1, VW - 40, mid, C_MUTED, b);
}

static void page_header(const char *title, const char *crumb)
{
    text(UI_STENCIL, 44, 2, 64, 70, C_TEXT, up(T(title)));
    if (crumb) text_r(UI_SEMIBOLD, 14, 2, VW - 64, 74, C_MUTED, crumb);
}

/* The detail panel: the option's name and its help. */
static float help_panel(float x, float y, float w, int op)
{
    const char *h = help(op);
    float pad = 18;
    /* measure, then draw */
    float body = 0;
    {
        char line[256] = "";
        int lines = 0;
        const char *s = h;
        while (*s) {
            while (*s == ' ') s++;
            const char *e = s;
            while (*e && *e != ' ') e++;
            char trial[256];
            snprintf(trial, sizeof trial, "%s%s%.*s", line, line[0] ? " " : "", (int)(e - s), s);
            if (line[0] && ui_text_width(UI_REGULAR, 17, 0, trial) > w - 2 * pad) {
                lines++;
                snprintf(line, sizeof line, "%.*s", (int)(e - s), s);
            } else {
                snprintf(line, sizeof line, "%s", trial);
            }
            s = e;
        }
        if (line[0]) lines++;
        body = lines * 25.0f;
    }
    float hgt = 52 + (body ? body + 14 : 0);
    fill(x, y, w, hgt, C_PANEL, 235);
    outline(x, y, w, hgt, 1, C_LINE);
    const char *name = up(label(op));
    text(UI_STENCIL, fit(UI_STENCIL, 20, 1, name, w - 2 * pad), 1, x + pad, y + 26, C_ACCENT, name);
    if (body) {
        fill(x, y + 50, w, 1, C_LINE, 255);
        wrap(UI_REGULAR, 17, x + pad, y + 60, w - 2 * pad, 25, C_BODY, h);
    }
    return hgt;
}

/* Value with the arrows of a changeable option. */
static void row_value(float right, float y, int selected, int op, uint32_t c, float size)
{
    char b[128];
    value(op, b, sizeof b);
    if (submenu(op)) {
        triangle(right - 6, y, 10, 0, c);
        return;
    }
    if (!b[0]) return;
    if (selected && !is_result(op)) {
        float w = ui_text_width(UI_BOLD, size, 0, b);
        text_r(UI_BOLD, size, 0, right - 22, y, c, b);
        text(UI_BOLD, size, 0, right - 14, y, c, "›");
        text(UI_BOLD, size, 0, right - 22 - w - 16, y, c, "‹");
    } else {
        text_r(UI_BOLD, size, 0, right, y, c, b);
    }
}

/* A controls row's key or button, as a key cap at the right; the row waiting for one blinks. */
static void bind_chip(float right, float y, int op, int selected)
{
    const char *t = bind_text(op);
    int waiting = m.capture == op, dim = !t && !waiting;
    if (waiting) t = T(on_gamepad_tab() ? "Press a button…" : "Press a key…");
    else if (!t) t = "—";
    float w = ui_text_width(UI_BOLD, 15, 0.5f, t) + 24, h = 26, x = right - w;
    if (waiting) {
        int on = SDL_GetTicks() / 400 % 2;
        fill(x, y - h / 2, w, h, on ? C_GROUND : C_BAR, 255);
        outline(x, y - h / 2, w, h, 2, C_ACCENT);
        text(UI_BOLD, 15, 0.5f, x + 12, y, C_ACCENT, t);
        return;
    }
    fill(x, y - h / 2, w, h, selected ? C_GROUND : C_INSET, 255);
    outline(x, y - h / 2, w, h, 1, selected ? C_GROUND : C_LINE);
    text(UI_BOLD, 15, 0.5f, x + 12, y, dim ? C_DIM : C_TEXT, t);
}

/* ------------------------------------------------------------------ drawing: the controls' pictures */
/* The Keyboard and Gamepad tabs draw the device on the right: every key or button bound for the
   tab's player (amber), to a shortcut (cyan) or to the other player (faint amber); the selected
   row's one blinks, with its name. Drawn with shapes, no images. */
#define C_HK 0x6FC3D3u                         /* the shortcuts */

/* A filled circle. */
static void disc(float cx, float cy, float r, uint32_t c, int a)
{
    SDL_FColor fc = {(c >> 16 & 255) / 255.0f, (c >> 8 & 255) / 255.0f, (c & 255) / 255.0f, a / 255.0f};
    SDL_Vertex v[26];
    int idx[72];
    v[0] = (SDL_Vertex){{cx, cy}, fc, {0, 0}};
    for (int i = 0; i < 24; i++) {
        float t = i * 6.2831853f / 24;
        v[i + 1] = (SDL_Vertex){{cx + SDL_cosf(t) * r, cy + SDL_sinf(t) * r}, fc, {0, 0}};
        idx[3 * i] = 0;
        idx[3 * i + 1] = i + 1;
        idx[3 * i + 2] = (i + 1) % 24 + 1;
    }
    SDL_RenderGeometry(R, NULL, v, 25, idx, 72);
}

/* A line of thickness t. */
static void line(float x0, float y0, float x1, float y1, float t, uint32_t c, int a)
{
    float dx = x1 - x0, dy = y1 - y0, l = SDL_sqrtf(dx * dx + dy * dy);
    if (l <= 0) return;
    float nx = -dy / l * t / 2, ny = dx / l * t / 2;
    float p[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
    poly(p, 4, c, a);
}

/* An arrow head pointing right (0), up (1), down (2) or left (3). */
static void arrow(float x, float y, float s, int dir, uint32_t c)
{
    if (dir < 3) { triangle(x, y, s, dir, c); return; }
    float h = s / 2, p[6] = {x + h * 0.8f, y - h, x - h * 0.9f, y, x + h * 0.8f, y + h};
    poly(p, 3, c, 255);
}

/* What a key does for the tab's player: 1 a game action, 2 a shortcut, 3 the other player's
   action, 0 nothing; *name: its label. */
static int key_use(SDL_Scancode sc, const char **name)
{
    if (sc == SDL_SCANCODE_UNKNOWN) return 0;
    for (int a = 0; a < ACT_COUNT; a++)
        if (cfg.key[bind_player][a] == sc) { *name = T(ACTION_LABEL[a]); return 1; }
    for (int h = 0; h < HK_COUNT; h++)
        if (cfg.hk_key[h] == sc) { *name = T(HOTKEY_LABEL[h]); return 2; }
    for (int a = 0; a < ACT_COUNT; a++)
        if (cfg.key[!bind_player][a] == sc) { *name = T(ACTION_LABEL[a]); return 3; }
    return 0;
}

static int pad_use(int in, const char **name)
{
    if (in == PAD_NONE) return 0;
    for (int a = 0; a < ACT_COUNT; a++)
        if (cfg.button[bind_player][a] == in) { *name = T(ACTION_LABEL[a]); return 1; }
    for (int h = 0; h < HK_COUNT; h++)
        if (cfg.hk_pad[h] == in) { *name = T(HOTKEY_LABEL[h]); return 2; }
    return 0;
}

static uint32_t use_color(int use) { return use == 2 ? C_HK : C_ACCENT; }

/* The colors' legend and the selected row ("DOWN  ·  Z") under a picture; returns the height. */
static float art_footer(float x, float y, float w, int op)
{
    static const struct { const char *what; int use; } L[3] = {{"Player %d", 1}, {"Shortcut", 2}, {"Other player", 3}};
    float lx = x;
    for (int i = 0; i < (on_gamepad_tab() ? 2 : 3); i++) {
        char b[48];
        snprintf(b, sizeof b, T(L[i].what), bind_player + 1);
        fill(lx, y - 6, 12, 12, use_color(L[i].use), L[i].use == 3 ? 90 : 255);
        lx += 18;
        lx += text(UI_SEMIBOLD, 13, 0.5f, lx, y, C_MUTED, b) + 22;
    }
    if (!is_bind(op)) return 24;
    const char *t = bind_text(op);
    float y2 = y + 34;
    fill(x, y2 - 20, w, 40, C_GROUND, 255);
    outline(x, y2 - 20, w, 40, 1, C_LINE);
    const char *name = up(label(op));
    text(UI_STENCIL, fit(UI_STENCIL, 18, 1, name, w * 0.55f), 1, x + 14, y2, C_ACCENT, name);
    text_r(UI_BOLD, 17, 0.5f, x + w - 14, y2, t ? C_TEXT : C_DIM, t ? t : "—");
    return 24 + 54;
}

typedef struct { SDL_Scancode sc; float x, y, w; const char *cap; } KeyCap;
static const KeyCap KEYS[] = {
    {SDL_SCANCODE_ESCAPE, 0.0f, 0.0f, 1.0f, "Esc"},
    {SDL_SCANCODE_F1, 2.0f, 0.0f, 1.0f, "F1"},
    {SDL_SCANCODE_F2, 3.0f, 0.0f, 1.0f, "F2"},
    {SDL_SCANCODE_F3, 4.0f, 0.0f, 1.0f, "F3"},
    {SDL_SCANCODE_F4, 5.0f, 0.0f, 1.0f, "F4"},
    {SDL_SCANCODE_F5, 6.5f, 0.0f, 1.0f, "F5"},
    {SDL_SCANCODE_F6, 7.5f, 0.0f, 1.0f, "F6"},
    {SDL_SCANCODE_F7, 8.5f, 0.0f, 1.0f, "F7"},
    {SDL_SCANCODE_F8, 9.5f, 0.0f, 1.0f, "F8"},
    {SDL_SCANCODE_F9, 11.0f, 0.0f, 1.0f, "F9"},
    {SDL_SCANCODE_F10, 12.0f, 0.0f, 1.0f, "F10"},
    {SDL_SCANCODE_F11, 13.0f, 0.0f, 1.0f, "F11"},
    {SDL_SCANCODE_F12, 14.0f, 0.0f, 1.0f, "F12"},
    {SDL_SCANCODE_PRINTSCREEN, 15.25f, 0.0f, 1.0f, "PrtSc"},
    {SDL_SCANCODE_SCROLLLOCK, 16.25f, 0.0f, 1.0f, "ScrLk"},
    {SDL_SCANCODE_PAUSE, 17.25f, 0.0f, 1.0f, "Pause"},
    {SDL_SCANCODE_GRAVE, 0.0f, 1.25f, 1.0f, "`"},
    {SDL_SCANCODE_1, 1.0f, 1.25f, 1.0f, "1"},
    {SDL_SCANCODE_2, 2.0f, 1.25f, 1.0f, "2"},
    {SDL_SCANCODE_3, 3.0f, 1.25f, 1.0f, "3"},
    {SDL_SCANCODE_4, 4.0f, 1.25f, 1.0f, "4"},
    {SDL_SCANCODE_5, 5.0f, 1.25f, 1.0f, "5"},
    {SDL_SCANCODE_6, 6.0f, 1.25f, 1.0f, "6"},
    {SDL_SCANCODE_7, 7.0f, 1.25f, 1.0f, "7"},
    {SDL_SCANCODE_8, 8.0f, 1.25f, 1.0f, "8"},
    {SDL_SCANCODE_9, 9.0f, 1.25f, 1.0f, "9"},
    {SDL_SCANCODE_0, 10.0f, 1.25f, 1.0f, "0"},
    {SDL_SCANCODE_MINUS, 11.0f, 1.25f, 1.0f, "-"},
    {SDL_SCANCODE_EQUALS, 12.0f, 1.25f, 1.0f, "="},
    {SDL_SCANCODE_BACKSPACE, 13.0f, 1.25f, 2.0f, "Bksp"},
    {SDL_SCANCODE_INSERT, 15.25f, 1.25f, 1.0f, "Ins"},
    {SDL_SCANCODE_HOME, 16.25f, 1.25f, 1.0f, "Home"},
    {SDL_SCANCODE_PAGEUP, 17.25f, 1.25f, 1.0f, "PgUp"},
    {SDL_SCANCODE_TAB, 0.0f, 2.25f, 1.5f, "Tab"},
    {SDL_SCANCODE_Q, 1.5f, 2.25f, 1.0f, "Q"},
    {SDL_SCANCODE_W, 2.5f, 2.25f, 1.0f, "W"},
    {SDL_SCANCODE_E, 3.5f, 2.25f, 1.0f, "E"},
    {SDL_SCANCODE_R, 4.5f, 2.25f, 1.0f, "R"},
    {SDL_SCANCODE_T, 5.5f, 2.25f, 1.0f, "T"},
    {SDL_SCANCODE_Y, 6.5f, 2.25f, 1.0f, "Y"},
    {SDL_SCANCODE_U, 7.5f, 2.25f, 1.0f, "U"},
    {SDL_SCANCODE_I, 8.5f, 2.25f, 1.0f, "I"},
    {SDL_SCANCODE_O, 9.5f, 2.25f, 1.0f, "O"},
    {SDL_SCANCODE_P, 10.5f, 2.25f, 1.0f, "P"},
    {SDL_SCANCODE_LEFTBRACKET, 11.5f, 2.25f, 1.0f, "["},
    {SDL_SCANCODE_RIGHTBRACKET, 12.5f, 2.25f, 1.0f, "]"},
    {SDL_SCANCODE_BACKSLASH, 13.5f, 2.25f, 1.5f, "\\"},
    {SDL_SCANCODE_DELETE, 15.25f, 2.25f, 1.0f, "Del"},
    {SDL_SCANCODE_END, 16.25f, 2.25f, 1.0f, "End"},
    {SDL_SCANCODE_PAGEDOWN, 17.25f, 2.25f, 1.0f, "PgDn"},
    {SDL_SCANCODE_CAPSLOCK, 0.0f, 3.25f, 1.75f, "Caps"},
    {SDL_SCANCODE_A, 1.75f, 3.25f, 1.0f, "A"},
    {SDL_SCANCODE_S, 2.75f, 3.25f, 1.0f, "S"},
    {SDL_SCANCODE_D, 3.75f, 3.25f, 1.0f, "D"},
    {SDL_SCANCODE_F, 4.75f, 3.25f, 1.0f, "F"},
    {SDL_SCANCODE_G, 5.75f, 3.25f, 1.0f, "G"},
    {SDL_SCANCODE_H, 6.75f, 3.25f, 1.0f, "H"},
    {SDL_SCANCODE_J, 7.75f, 3.25f, 1.0f, "J"},
    {SDL_SCANCODE_K, 8.75f, 3.25f, 1.0f, "K"},
    {SDL_SCANCODE_L, 9.75f, 3.25f, 1.0f, "L"},
    {SDL_SCANCODE_SEMICOLON, 10.75f, 3.25f, 1.0f, ";"},
    {SDL_SCANCODE_APOSTROPHE, 11.75f, 3.25f, 1.0f, "'"},
    {SDL_SCANCODE_RETURN, 12.75f, 3.25f, 2.25f, "Enter"},
    {SDL_SCANCODE_LSHIFT, 0.0f, 4.25f, 2.25f, "Shift"},
    {SDL_SCANCODE_Z, 2.25f, 4.25f, 1.0f, "Z"},
    {SDL_SCANCODE_X, 3.25f, 4.25f, 1.0f, "X"},
    {SDL_SCANCODE_C, 4.25f, 4.25f, 1.0f, "C"},
    {SDL_SCANCODE_V, 5.25f, 4.25f, 1.0f, "V"},
    {SDL_SCANCODE_B, 6.25f, 4.25f, 1.0f, "B"},
    {SDL_SCANCODE_N, 7.25f, 4.25f, 1.0f, "N"},
    {SDL_SCANCODE_M, 8.25f, 4.25f, 1.0f, "M"},
    {SDL_SCANCODE_COMMA, 9.25f, 4.25f, 1.0f, ","},
    {SDL_SCANCODE_PERIOD, 10.25f, 4.25f, 1.0f, "."},
    {SDL_SCANCODE_SLASH, 11.25f, 4.25f, 1.0f, "/"},
    {SDL_SCANCODE_RSHIFT, 12.25f, 4.25f, 2.75f, "Shift"},
    {SDL_SCANCODE_UP, 16.25f, 4.25f, 1.0f, "^1"},
    {SDL_SCANCODE_LCTRL, 0.0f, 5.25f, 1.25f, "Ctrl"},
    {SDL_SCANCODE_LGUI, 1.25f, 5.25f, 1.25f, "Win"},
    {SDL_SCANCODE_LALT, 2.5f, 5.25f, 1.25f, "Alt"},
    {SDL_SCANCODE_SPACE, 3.75f, 5.25f, 6.25f, ""},
    {SDL_SCANCODE_RALT, 10.0f, 5.25f, 1.25f, "Alt"},
    {SDL_SCANCODE_RGUI, 11.25f, 5.25f, 1.25f, "Win"},
    {SDL_SCANCODE_APPLICATION, 12.5f, 5.25f, 1.25f, "Menu"},
    {SDL_SCANCODE_RCTRL, 13.75f, 5.25f, 1.25f, "Ctrl"},
    {SDL_SCANCODE_LEFT, 15.25f, 5.25f, 1.0f, "^3"},
    {SDL_SCANCODE_DOWN, 16.25f, 5.25f, 1.0f, "^2"},
    {SDL_SCANCODE_RIGHT, 17.25f, 5.25f, 1.0f, "^0"},
};

static void draw_keyboard(float x, float y, float w, int op)
{
    float u = w / 18.25f, g = SDL_max(1.5f, u * 0.08f);
    SDL_Scancode sel = is_bind(op) ? *key_slot(op) : SDL_SCANCODE_UNKNOWN;
    int blink = SDL_GetTicks() / 450 % 2;
    for (size_t i = 0; i < sizeof KEYS / sizeof KEYS[0]; i++) {
        const KeyCap *kc = &KEYS[i];
        float kx = x + kc->x * u, ky = y + kc->y * u, kw = kc->w * u - g, kh = u - g;
        const char *name = NULL;
        int use = key_use(kc->sc, &name);
        uint32_t ink = use ? C_GROUND : C_DIM;
        if (use) fill(kx, ky, kw, kh, use_color(use), use == 3 ? 90 : 255);
        else { fill(kx, ky, kw, kh, C_INSET, 255); outline(kx, ky, kw, kh, 1, C_LINE); }
        if (use == 3) ink = C_TEXT;
        if (kc->sc == sel) outline(kx - 2, ky - 2, kw + 4, kh + 4, 2, blink ? C_TEXT : use_color(use ? use : 1));
        if (kc->cap[0] == '^') arrow(kx + kw / 2, ky + kh / 2, u * 0.34f, kc->cap[1] - '0', ink);
        else if (kc->cap[0]) {
            float s = u * (strlen(kc->cap) > 2 ? 0.30f : 0.40f);
            text_c(UI_SEMIBOLD, fit(UI_SEMIBOLD, s, 0, kc->cap, kw - 4), 0, kx + kw / 2, ky + kh / 2, ink, kc->cap);
        }
    }
}

/* The gamepad, in a 200 x 130 box: (input, x, y) of each part, the callouts' side by x. */
typedef struct { int in; float x, y; } PadPart;
static const PadPart PAD_PARTS[] = {
    {PAD_LT, 38, 6}, {PAD_RT, 162, 6}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 40, 20},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 160, 20}, {SDL_GAMEPAD_BUTTON_LEFT_STICK, 52, 58},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, 76, 82}, {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 66, 92},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 86, 92}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 76, 102},
    {SDL_GAMEPAD_BUTTON_BACK, 86, 54}, {SDL_GAMEPAD_BUTTON_GUIDE, 100, 40}, {SDL_GAMEPAD_BUTTON_MISC1, 100, 62},
    {SDL_GAMEPAD_BUTTON_START, 114, 54}, {SDL_GAMEPAD_BUTTON_NORTH, 148, 46}, {SDL_GAMEPAD_BUTTON_WEST, 136, 58},
    {SDL_GAMEPAD_BUTTON_EAST, 160, 58}, {SDL_GAMEPAD_BUTTON_SOUTH, 148, 70},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, 124, 92},
    {PAD_LS_UP, 52, 44}, {PAD_LS_DOWN, 52, 72}, {PAD_LS_LEFT, 38, 58}, {PAD_LS_RIGHT, 66, 58},
    {PAD_RS_UP, 124, 78}, {PAD_RS_DOWN, 124, 106}, {PAD_RS_LEFT, 110, 92}, {PAD_RS_RIGHT, 138, 92},
};
#define N_PAD_PARTS ((int)(sizeof PAD_PARTS / sizeof PAD_PARTS[0]))

/* An arcade stick (the same 200 x 130 box): the lever (the D-pad or the left stick, whichever the
   stick's switch makes it) and eight buttons in two rows as XInput sticks map them (Mayflash F300:
   X Y RB LB over A B RT LT), the small buttons on top. */
static const PadPart ARCADE_PARTS[] = {
    {SDL_GAMEPAD_BUTTON_DPAD_UP, 46, 48}, {SDL_GAMEPAD_BUTTON_DPAD_DOWN, 46, 100},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, 20, 74}, {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, 72, 74},
    {PAD_LS_UP, 46, 48}, {PAD_LS_DOWN, 46, 100}, {PAD_LS_LEFT, 20, 74}, {PAD_LS_RIGHT, 72, 74},
    {SDL_GAMEPAD_BUTTON_WEST, 106, 60}, {SDL_GAMEPAD_BUTTON_NORTH, 130, 54},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, 154, 54}, {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, 178, 58},
    {SDL_GAMEPAD_BUTTON_SOUTH, 102, 88}, {SDL_GAMEPAD_BUTTON_EAST, 126, 82},
    {PAD_RT, 150, 82}, {PAD_LT, 174, 86},
    {SDL_GAMEPAD_BUTTON_GUIDE, 118, 24}, {SDL_GAMEPAD_BUTTON_BACK, 138, 24},
    {SDL_GAMEPAD_BUTTON_START, 158, 24}, {SDL_GAMEPAD_BUTTON_MISC1, 178, 24},
};
#define N_ARCADE_PARTS ((int)(sizeof ARCADE_PARTS / sizeof ARCADE_PARTS[0]))

/* A face button's mark: the letter, or PlayStation's shapes. */
static void face_mark(int in, float cx, float cy, float r, uint32_t c, uint32_t bg)
{
    SDL_GamepadType t = pad_type();
    float s = r * 0.5f, th = SDL_max(1.2f, r * 0.16f);
    switch (SDL_GetGamepadButtonLabelForType(t, (SDL_GamepadButton)in)) {
    case SDL_GAMEPAD_BUTTON_LABEL_CROSS:
        line(cx - s, cy - s, cx + s, cy + s, th, c, 255);
        line(cx - s, cy + s, cx + s, cy - s, th, c, 255);
        break;
    case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: disc(cx, cy, s * 1.1f, c, 255); disc(cx, cy, s * 1.1f - th, bg, 255); break;
    case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: outline(cx - s, cy - s, 2 * s, 2 * s, th, c); break;
    case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE:
        line(cx, cy - s, cx + s, cy + s * 0.8f, th, c, 255);
        line(cx + s, cy + s * 0.8f, cx - s, cy + s * 0.8f, th, c, 255);
        line(cx - s, cy + s * 0.8f, cx, cy - s, th, c, 255);
        break;
    default: text_c(UI_BOLD, r * 1.1f, 0, cx, cy, c, pad_label(in)); break;
    }
}

static void draw_gamepad(float x, float y, float w, float h, int op)
{
    float col = SDL_min(130, w * 0.25f), bw = w - 2 * col, s = SDL_min(bw / 190, h / 150);
    float bx = x + col + (bw - 200 * s) / 2, by = y + (h - 130 * s) / 2;
    int sel = is_bind(op) ? *pad_slot(op) : PAD_NONE, blink = SDL_GetTicks() / 450 % 2;
    int arcade = cfg.pad_style[bind_player] == PAD_STYLE_ARCADE;
    const PadPart *PARTS = arcade ? ARCADE_PARTS : PAD_PARTS;
    int n_parts = arcade ? N_ARCADE_PARTS : N_PAD_PARTS;
#define PX(v) (bx + (v) * s)
#define PY(v) (by + (v) * s)
    const uint32_t edge = 0x4A545Bu, body = 0x272E33u;
    if (arcade) {                                               /* the case and the lever */
        fill(PX(0), PY(10), 200 * s, 112 * s, edge, 255);
        fill(PX(2), PY(12), 196 * s, 108 * s, body, 255);
        fill(PX(2), PY(36), 196 * s, 1.5f * s, edge, 255);
        disc(PX(46), PY(74), 15 * s, edge, 255);                /* the lever's base */
        line(PX(46), PY(74), PX(46), PY(64), 4 * s, 0x8A9298u, 255);
        disc(PX(46), PY(62), 10 * s, 0xC0392Bu, 255);           /* the ball top */
        disc(PX(43), PY(59), 3 * s, 0xE8786Cu, 255);
    } else {
    /* the body: two grips and the middle */
    disc(PX(52), PY(84), 40 * s, edge, 255);
    disc(PX(148), PY(84), 40 * s, edge, 255);
    disc(PX(52), PY(52), 24 * s, edge, 255);
    disc(PX(148), PY(52), 24 * s, edge, 255);
    fill(PX(46), PY(28), 108 * s, 68 * s, edge, 255);
    disc(PX(52), PY(84), 38 * s, body, 255);
    disc(PX(148), PY(84), 38 * s, body, 255);
    disc(PX(52), PY(52), 22 * s, body, 255);
    disc(PX(148), PY(52), 22 * s, body, 255);
    fill(PX(47), PY(30), 106 * s, 64 * s, body, 255);
    }
    /* the callouts first (their lines go under the parts) */
    float ly[2] = {by - 10, by - 10};
    int n_side[2] = {0, 0};
    for (int i = 0; i < n_parts; i++) {
        const char *name = NULL;
        if (pad_use(PARTS[i].in, &name)) n_side[PARTS[i].x >= 100]++;
    }
    float span = SDL_min(h, SDL_max(130 * s + 20, 24.0f * SDL_max(n_side[0], n_side[1])));
    float step[2] = {span / SDL_max(1, n_side[0]), span / SDL_max(1, n_side[1])};
    ly[0] = ly[1] = y + (h - span) / 2;
    /* each side's callouts top to bottom by their part's height, so the lines do not cross */
    int order[48], n_order = 0;
    for (int i = 0; i < n_parts && i < 48; i++) {
        int k = n_order++;
        while (k > 0 && PARTS[order[k - 1]].y > PARTS[i].y) { order[k] = order[k - 1]; k--; }
        order[k] = i;
    }
    for (int o = 0; o < n_order; o++) {
        const PadPart *pp = &PARTS[order[o]];
        const char *name = NULL;
        int use = pad_use(pp->in, &name), side = pp->x >= 100;
        if (!use) continue;
        float ty = ly[side] + step[side] / 2, px = PX(pp->x), py = PY(pp->y);
        ly[side] += step[side];
        float tx = side ? x + w - col + 8 : x + col - 8;
        uint32_t c = pp->in == sel && blink ? C_TEXT : use_color(use);
        line(px, py, side ? tx - 4 : tx + 4, ty, 1.2f, c, 200);
        float size = fit(UI_SEMIBOLD, 13, 0, name, col - 14);
        if (side) text(UI_SEMIBOLD, size, 0, tx, ty, c, name);
        else text_r(UI_SEMIBOLD, size, 0, tx, ty, c, name);
    }
    /* the parts: bound ones in their color, the selected row's one blinks */
    for (int i = 0; i < n_parts; i++) {
        const PadPart *pp = &PARTS[i];
        const char *name = NULL;
        int use = pad_use(pp->in, &name), in = pp->in;
        uint32_t c = use ? use_color(use) : C_DIM, ink = use ? C_GROUND : C_DIM;
        if (in == sel && blink) c = C_TEXT, ink = C_GROUND;
        float px = PX(pp->x), py = PY(pp->y);
        if (arcade) {                                           /* the lever's directions, round buttons */
            int dir_in = (in >= SDL_GAMEPAD_BUTTON_DPAD_UP && in <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) ||
                         (in >= PAD_LS_UP && in <= PAD_LS_RIGHT);
            if (dir_in) {
                int k = in >= PAD_LS_UP ? in - PAD_LS_UP : in - SDL_GAMEPAD_BUTTON_DPAD_UP;   /* up down left right */
                if (use || in == sel) arrow(px, py, 9 * s, (int[]){1, 2, 3, 0}[k], c);
            } else if (pp->y < 36) {
                disc(px, py, 4.5f * s, use || in == sel ? c : edge, 255);
            } else {
                disc(px, py, 10.5f * s, use || in == sel ? c : edge, 255);
                disc(px, py, 8.5f * s, use || in == sel ? c : 0x343C42u, 255);
                text_c(UI_BOLD, 7 * s, 0, px, py, use || in == sel ? C_GROUND : C_DIM, pad_label(in));
            }
            continue;
        }
        if (in >= PAD_LS_UP && in <= PAD_RS_RIGHT) {          /* a stick's direction: only when bound */
            if (use || in == sel) arrow(px, py, 7 * s, (int[]){1, 2, 3, 0}[(in - PAD_LS_UP) % 4], c);
        } else if (in == PAD_LT || in == PAD_RT) {
            fill(px - 14 * s, py - 5 * s, 28 * s, 10 * s, c, use || in == sel ? 255 : 120);
            text_c(UI_BOLD, 7 * s, 0, px, py, use || in == sel ? C_GROUND : C_INSET, pad_label(in));
        } else if (in == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER || in == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
            fill(px - 16 * s, py - 4 * s, 32 * s, 8 * s, c, use || in == sel ? 255 : 120);
            text_c(UI_BOLD, 6 * s, 0, px, py, use || in == sel ? C_GROUND : C_INSET, pad_label(in));
        } else if (in == SDL_GAMEPAD_BUTTON_LEFT_STICK || in == SDL_GAMEPAD_BUTTON_RIGHT_STICK) {
            disc(px, py, 12 * s, use || in == sel ? c : edge, 255);
            disc(px, py, 8 * s, body, 255);
        } else if (in >= SDL_GAMEPAD_BUTTON_DPAD_UP && in <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
            float dx = px - PX(76), dy = py - PY(92);           /* the arm's direction from the centre */
            fill(px - 5 * s, py - 5 * s, 10 * s, 10 * s, use || in == sel ? c : edge, 255);
            int dir = dy < 0 ? 1 : dy > 0 ? 2 : dx > 0 ? 0 : 3;
            arrow(px, py, 6 * s, dir, use || in == sel ? C_GROUND : C_DIM);
        } else if (in == SDL_GAMEPAD_BUTTON_NORTH || in == SDL_GAMEPAD_BUTTON_WEST ||
                   in == SDL_GAMEPAD_BUTTON_EAST || in == SDL_GAMEPAD_BUTTON_SOUTH) {
            disc(px, py, 7 * s, use || in == sel ? c : edge, 255);
            face_mark(in, px, py, 7 * s, use || in == sel ? ink : C_DIM, use || in == sel ? c : edge);
        } else {                                                /* Back, Start, Guide, Share */
            float r = in == SDL_GAMEPAD_BUTTON_GUIDE ? 6 * s : 4 * s;
            disc(px, py, r, use || in == sel ? c : edge, 255);
        }
    }
#undef PX
#undef PY
}

/* The right side of the controls tabs: the device with the bindings. */
static void controls_art(float x, float y, float w, float h, int op)
{
    fill(x, y, w, h, C_PANEL, 235);
    outline(x, y, w, h, 1, C_LINE);
    char pl[48], t[96];
    snprintf(pl, sizeof pl, T("Player %d"), bind_player + 1);
    snprintf(t, sizeof t, "%s  ·  %s", T(on_gamepad_tab() ? "GAMEPAD" : "KEYBOARD"), pl);
    text(UI_STENCIL, 18, 1, x + 18, y + 26, C_ACCENT, up(t));
    fill(x, y + 50, w, 1, C_LINE, 255);
    float pad = 18, foot = is_bind(op) ? 92 : 40, ay = y + 64, ah = h - 64 - foot - 8;
    if (on_gamepad_tab() && !fe_gamepad(bind_player))      /* the picture is the usual layout */
        text_c(UI_SEMIBOLD, 12, 1, x + w / 2, ay + 4, C_MUTED, up(T("No gamepad connected")));
    if (on_gamepad_tab()) draw_gamepad(x + pad, ay, w - 2 * pad, ah, op);
    else draw_keyboard(x + pad, ay + (ah - (w - 2 * pad) / 18.25f * 6.25f) / 2, w - 2 * pad, op);
    art_footer(x + pad, y + h - foot + 12, w - 2 * pad, op);
}

/* ------------------------------------------------------------------ drawing: layouts */
static int sel_op(const int *ops, int n) { return n ? ops[m.sel[m.level] < n ? m.sel[m.level] : 0] : -1; }

static void draw_home(const Page *p)
{
    int ops[32], n = page_ops(p, ops);
    if (m.sel[m.level] >= n) m.sel[m.level] = 0;
    int pause = p == &P_PAUSE, cur = sel_op(ops, n);
    float list_x = 64, list_w = 400, top = 236;
    if (pause) {
        float pw = SDL_max(500, VW * 0.4f);
        fill(0, 0, pw, VH - 56, C_BAR, 240);
        fill(pw - 2, 0, 2, VH - 56, C_ACCENT, 255);
        text(UI_STENCIL, 52, 3, 64, 86, C_ACCENT, up(T("PAUSE")));
        char b[96];
        int mission = fe_mission_now();
        if (mission) snprintf(b, sizeof b, T("MISSION %d · PLAYER 1"), mission);
        else snprintf(b, sizeof b, "%s", game_profile.title ? game_profile.title : "");
        text(UI_SEMIBOLD, 15, 2, 64, 128, C_MUTED, up(b));
        top = 176;
    } else {
        hazard(0, 0, 12, VH - 56);
        SDL_Texture *logo = logo_texture();
        if (logo) {
            float lw, lh;
            SDL_GetTextureSize(logo, &lw, &lh);
            float e = SDL_min(400 / lw, 160 / lh);
            SDL_FRect d = {64, 40 + (160 - lh * e) / 2, lw * e, lh * e};
            SDL_RenderTexture(R, logo, NULL, &d);
        } else {
            text(UI_STENCIL, 46, 2, 64, 100, C_TEXT, up(game_profile.title ? game_profile.title : ""));
            if (game_profile.subtitle) text(UI_SEMIBOLD, 15, 2, 66, 146, C_MUTED, up(game_profile.subtitle));
        }
    }
    /* the list */
    float y = top;
    for (int i = 0; i < n; i++) {
        int op = ops[i], s = i == m.sel[m.level];
        float h = s ? 50 : 46;
        add_hit(list_x, y - 2, list_w, h + 4, HIT_ITEM, i, 0);
        if (s) {
            float q[8] = {list_x, y, list_x + list_w, y, list_x + list_w - 16, y + h, list_x, y + h};
            poly(q, 4, C_ACCENT, 255);
            triangle(list_x + 26, y + h / 2, 14, 0, C_GROUND);
            text(UI_BOLD, 22, 2, list_x + 46, y + h / 2, C_GROUND, up(label(op)));
            char b[64];
            value(op, b, sizeof b);
            if (b[0]) {
                text_r(UI_BOLD, 17, 0, list_x + list_w - 40, y + h / 2, C_GROUND, b);
                text(UI_BOLD, 17, 0, list_x + list_w - 34, y + h / 2, C_GROUND, "›");
                float bw = ui_text_width(UI_BOLD, 17, 0, b);
                text_r(UI_BOLD, 17, 0, list_x + list_w - 46 - bw, y + h / 2, C_GROUND, "‹");
                add_hit(list_x + list_w - 76 - bw, y, 34, h, HIT_ARROW, i, -1);
                add_hit(list_x + list_w - 40, y, 36, h, HIT_ARROW, i, 1);
            }
        } else {
            uint32_t c = op == OP_QUIT ? C_MUTED : C_TEXT;
            text(UI_SEMIBOLD, 20, 2, list_x + 46, y + h / 2, c, up(label(op)));
            char b[64];
            value(op, b, sizeof b);
            if (b[0]) text_r(UI_SEMIBOLD, 16, 0, list_x + list_w - 24, y + h / 2, C_MUTED, b);
        }
        y += h + 4;
    }
    /* the detail panel */
    float px = SDL_max(list_x + list_w + 56, VW - 64 - 400), pw = VW - 64 - px;
    if (pw < 260 || cur < 0) return;
    FeResumeInfo ri;
    if (cur == OP_RESUME && fe_resume_info(&ri)) {
        float py = top, pad = 18;
        SDL_Texture *thumb = fe_resume_thumbnail();
        float th = thumb ? (pw - 2 * pad) * 224 / 320 * 0.82f : 0;
        float hgt = 52 + (thumb ? th + 16 : 0) + 130;
        fill(px, py, pw, hgt, C_PANEL, 240);
        outline(px, py, pw, hgt, 1, C_LINE);
        const char *head = up(T("Game in progress")), *tag = up(T("Autosaved"));
        float tag_w = ui_text_width(UI_SEMIBOLD, 13, 1, tag);
        float room = pw - 2 * pad - tag_w - 16, size = 20;
        if (ui_text_width(UI_STENCIL, 20, 1, head) > room) tag_w = 0, room = pw - 2 * pad;   /* no tag */
        size = fit(UI_STENCIL, size, 1, head, room);
        text(UI_STENCIL, size, 1, px + pad, py + 26, C_ACCENT, head);
        if (tag_w) text_r(UI_SEMIBOLD, 13, 1, px + pw - pad, py + 26, C_MUTED, tag);
        fill(px, py + 50, pw, 1, C_LINE, 255);
        float gy = py + 52;
        if (thumb) {
            float tw = th * 320 / 224;
            SDL_FRect d = {px + (pw - tw) / 2, py + 66, tw, th};
            fill(d.x - 1, d.y - 1, d.w + 2, d.h + 2, C_LINE, 255);
            SDL_RenderTexture(R, thumb, NULL, &d);
            gy = d.y + d.h + 4;
        }
        char v[4][64] = {"-", "-", "-", "-"};
        if (ri.mission) snprintf(v[0], sizeof v[0], "%d", ri.mission);
        if (game_profile.score_p1) snprintf(v[1], sizeof v[1], "%u", (unsigned)ri.score);
        if (ri.saved) {
            SDL_DateTime dt;
            if (SDL_TimeToDateTime((SDL_Time)ri.saved, &dt, true)) {
                if (cfg.language == LANG_ES)
                    snprintf(v[2], sizeof v[2], "%02d/%02d/%04d", dt.day, dt.month, dt.year);
                else
                    snprintf(v[2], sizeof v[2], "%04d-%02d-%02d", dt.year, dt.month, dt.day);
                snprintf(v[3], sizeof v[3], "%02d:%02d", dt.hour, dt.minute);
            }
        }
        static const char *const K[4] = {"MISSION", "SCORE", "DATE", "TIME"};
        for (int i = 0; i < 4; i++) {
            float cx = px + pad + (i % 2) * (pw - 2 * pad) / 2, cy = gy + 22 + (i / 2) * 56;
            text(UI_SEMIBOLD, 12, 2, cx, cy, C_MUTED, up(T(K[i])));
            text(UI_BOLD, 20, 0, cx, cy + 24, C_TEXT, v[i]);
        }
        return;
    }
    float hgt = help_panel(px, top, pw, cur);
    if (pause && game_profile.score_p1 && fe_mission_now()) {
        float sy = top + hgt + 20, sw = pw;
        fill(px, sy, sw, 76, C_PANEL, 240);
        outline(px, sy, sw, 76, 1, C_LINE);
        fill(px + sw / 2, sy, 1, 76, C_LINE, 255);
        char b[32];
        text(UI_SEMIBOLD, 12, 2, px + 18, sy + 24, C_MUTED, up(T("MISSION")));
        snprintf(b, sizeof b, "%d", fe_mission_now());
        text(UI_BOLD, 22, 0, px + 18, sy + 52, C_TEXT, b);
        text(UI_SEMIBOLD, 12, 2, px + sw / 2 + 18, sy + 24, C_MUTED, up(T("SCORE")));
        snprintf(b, sizeof b, "%u", (unsigned)fe_score_p1());
        text(UI_BOLD, 22, 0, px + sw / 2 + 18, sy + 52, C_TEXT, b);
    }
}

static void draw_list(const Page *p)
{
    int ops[32], n = page_ops(p, ops);
    if (m.sel[m.level] >= n) m.sel[m.level] = 0;
    hazard(0, 0, 12, VH - 56);
    int tab = tab_of(p);
    page_header(tab >= 0 ? "SETTINGS" : p->title, NULL);
    float top = 128;
    if (tab >= 0) {
        float x = 64, y = 112, h = 46;
        fill(64, y + h, VW - 128, 1, C_LINE, 255);
        float kw = keycap(x, y + h / 2, gamepad_hints ? "LB" : "Q");
        add_hit(x - 4, y, kw + 8, h, HIT_NAV, 9, 0);
        x += kw + 14;
        for (int i = 0; i < N_TABS; i++) {
            const char *t = up(T(SETTINGS_TABS[i]->title));
            float w = ui_text_width(UI_BOLD, 18, 2, t) + 44;
            int on = i == tab;
            add_hit(x, y, w, h, HIT_TAB, i, 0);
            text(on ? UI_BOLD : UI_SEMIBOLD, 18, 2, x + 22, y + h / 2, on ? C_ACCENT : C_TEXT, t);
            if (on) fill(x, y + h - 3, w, 3, C_ACCENT, 255);
            x += w;
        }
        kw = keycap(x + 14, y + h / 2, gamepad_hints ? "RB" : "E");
        add_hit(x + 10, y, kw + 8, h, HIT_NAV, 10, 0);
        top = 186;
    }
    int binds = p == &P_KEYBOARD || p == &P_GAMEPAD;
    float list_w = binds ? SDL_max(400, (VW - 128) * 0.44f) : SDL_max(480, (VW - 128) * 0.56f);
    float avail_h = VH - 56 - 24 - top;
    float row_h = SDL_min(48, avail_h / (n ? n : 1));
    int first = 0, shown = n, sel = m.sel[m.level];
    if (row_h < 34) {                       /* a long list: rows of 36, scrolled to keep the selection in view */
        row_h = 36;
        shown = (int)(avail_h / row_h);
        first = m.first[m.level];
        if (sel - 1 < first) first = sel - 1;
        if (sel + 1 >= first + shown) first = sel + 2 - shown;
        first = SDL_max(0, SDL_min(first, n - shown));
        m.first[m.level] = first;
        if (first > 0) triangle(64 + list_w + 20, top + 10, 12, 1, C_MUTED);
        if (first + shown < n) triangle(64 + list_w + 20, top + shown * row_h - 10, 12, 2, C_MUTED);
    }
    float size = row_h >= 44 ? 19 : 17;
    for (int i = first; i < first + shown && i < n; i++) {
        int op = ops[i], s = i == sel;
        float y = top + (i - first) * row_h;
        if (is_header(op)) {                 /* a section title */
            text(UI_SEMIBOLD, 13, 2, 82, y + row_h * 0.6f, C_ACCENT, up(label(op)));
            fill(64, y + row_h - 1, list_w, 1, C_LINE, 255);
            continue;
        }
        add_hit(64, y, list_w, row_h, HIT_ITEM, i, 0);
        if (is_bind(op)) {
            fill(64, y, list_w, row_h, s ? C_ACCENT : C_GROUND, s ? 255 : 150);
            if (!s) fill(64, y + row_h - 1, list_w, 1, C_ROW_LINE, 255);
            text(s ? UI_BOLD : UI_SEMIBOLD, size, 0, 82, y + row_h / 2, s ? C_GROUND : C_TEXT, label(op));
            bind_chip(64 + list_w - 14, y + row_h / 2, op, s);
            continue;
        }
        if (s) {
            fill(64, y, list_w, row_h, C_ACCENT, 255);
            text(UI_BOLD, size, 0, 82, y + row_h / 2, C_GROUND, label(op));
            row_value(64 + list_w - 18, y + row_h / 2, 1, op, C_GROUND, size - 1);
            char b[128];
            value(op, b, sizeof b);
            if (b[0] && !submenu(op) && !is_result(op)) {
                float bw = ui_text_width(UI_BOLD, size - 1, 0, b), right = 64 + list_w - 18;
                add_hit(right - 22 - bw - 30, y, 32, row_h, HIT_ARROW, i, -1);
                add_hit(right - 20, y, 38, row_h, HIT_ARROW, i, 1);
            }
        } else {
            fill(64, y, list_w, row_h, C_GROUND, 150);
            fill(64, y + row_h - 1, list_w, 1, C_ROW_LINE, 255);
            text(UI_SEMIBOLD, size, 0, 82, y + row_h / 2, C_TEXT, label(op));
            row_value(64 + list_w - 18, y + row_h / 2, 0, op, C_TEXT, size - 1);
        }
    }
    float ax = 64 + list_w + (binds ? 36 : 48), aw = VW - 64 - ax;
    if (binds && aw >= 300) controls_art(ax, top, aw, SDL_min(avail_h, 440), sel_op(ops, n));
    else if (n && aw >= 220 && help(sel_op(ops, n))[0]) help_panel(ax, top, aw, sel_op(ops, n));
}

static void draw_modes(const Page *p)
{
    int ops[32], n = page_ops(p, ops);
    if (m.sel[m.level] >= n) m.sel[m.level] = 0;
    hazard(0, 0, 12, VH - 56);
    char crumb[96];
    snprintf(crumb, sizeof crumb, "%s › %s", up(T("Home")), up(T("Modes")));
    page_header(p->title, crumb);
    float gap = 32, x0 = 64, cw = (VW - 128 - gap * (n - 1)) / (n ? n : 1), y = 140, ch = 440, art = 200;
    if (cw > 380) {                                   /* few cards (Metal Slug X: Arcade only): centred */
        cw = 380;
        x0 = (VW - n * cw - gap * (n - 1)) / 2;
    }
    for (int i = 0; i < n; i++) {
        int op = ops[i], s = i == m.sel[m.level], locked = op == OP_BOSS_RUSH && !progress_rush_unlocked();
        float x = x0 + i * (cw + gap);
        add_hit(x, y, cw, ch, HIT_ITEM, i, 0);
        fill(x, y, cw, ch, locked ? C_PANEL_DIM : C_PANEL, 245);
        fill(x, y, cw, art, C_INSET, 255);
        fill(x, y + art, cw, 1, C_LINE, 255);
        float cx = x + cw / 2, cy = y + art / 2;
        char big[64] = "", small[64] = "";
        if (op == OP_ARCADE) {
            value(op, big, sizeof big);
            snprintf(small, sizeof small, "%s", up(T("Continues")));
        } else if (op == OP_MISSIONS) {
            snprintf(big, sizeof big, "%d / %u", progress_unlocked(), (unsigned)game_profile.missions);
            snprintf(small, sizeof small, "%s", up(T("Unlocked")));
        } else if (op == OP_BOSS_RUSH) {
            snprintf(big, sizeof big, "%d", bossrush_total());
            snprintf(small, sizeof small, "%s", up(T("Bosses")));
        }
        if (locked) {
            lock_icon(cx, cy - 14, 44, C_DIM);
            /* a mode: masculine in Spanish ("Locked" is the missions', feminine) */
            text_c(UI_SEMIBOLD, 14, 2, cx, cy + 34, C_DIM, up(cfg.language == LANG_ES ? "Bloqueado" : "Locked"));
        } else {
            text_c(UI_SEMIBOLD, 13, 2, cx, cy - 46, C_MUTED, small);
            float size = 58, w = ui_text_width(UI_STENCIL, size, 1, big);
            if (w > cw - 100) { size *= (cw - 100) / w; w = cw - 100; }
            text_c(UI_STENCIL, size, 1, cx, cy + 12, s ? C_ACCENT : C_TEXT, big);
            if (op == OP_ARCADE && s) {
                triangle(cx - w / 2 - 26, cy + 12, 16, 1, C_ACCENT);
                triangle(cx + w / 2 + 26, cy + 12, 16, 2, C_ACCENT);
                add_hit(cx - w / 2 - 46, cy - 18, 40, 60, HIT_ARROW, i, 1);    /* up: more */
                add_hit(cx + w / 2 + 6, cy - 18, 40, 60, HIT_ARROW, i, -1);
            }
        }
        const char *name = up(label(op));
        text(UI_STENCIL, fit(UI_STENCIL, 28, 1, name, cw - 40), 1, x + 20, y + art + 36,
             locked ? C_DIM : s ? C_ACCENT : C_TEXT, name);
        wrap(UI_REGULAR, 16, x + 20, y + art + 64, cw - 40, 23, locked ? C_DIM : C_BODY,
             locked ? T("Complete the game in Arcade or Mission mode to unlock it.") : help(op));
        char rec[64] = "";
        if (op == OP_ARCADE && records_supported() && records_arcade_score())
            snprintf(rec, sizeof rec, "%u", (unsigned)records_arcade_score());
        if (op == OP_BOSS_RUSH && records_rush_frames()) records_time_text(records_rush_frames(), rec, sizeof rec);
        else if (op == OP_BOSS_RUSH && records_rush_defeated())
            snprintf(rec, sizeof rec, "%d / %d", records_rush_defeated(), bossrush_total());
        if (rec[0]) {
            const char *b = rec;
            fill(x + 20, y + ch - 62, cw - 40, 42, C_GROUND, 255);
            outline(x + 20, y + ch - 62, cw - 40, 42, 1, C_LINE);
            text(UI_SEMIBOLD, 13, 2, x + 32, y + ch - 41, C_MUTED, up(T("Record")));
            text_r(UI_BOLD, 18, 0, x + cw - 32, y + ch - 41, C_TEXT, b);
        }
        if (s) outline(x - 2, y - 2, cw + 4, ch + 4, 3, C_ACCENT);
        else outline(x, y, cw, ch, 1, C_LINE);
    }
}

static void draw_missions(const Page *p)
{
    int ops[32], n = page_ops(p, ops);
    if (m.sel[m.level] >= n) m.sel[m.level] = 0;
    hazard(0, 0, 12, VH - 56);
    char crumb[128];
    snprintf(crumb, sizeof crumb, "%s › %s › %s", up(T("Home")), up(T("Modes")), up(T("Mission")));
    page_header(p->title, crumb);
    int unlocked = progress_unlocked();
    float gap = 20, cw = (VW - 128 - gap * (n - 1)) / (n ? n : 1), y = 140, ch = 290, art = 180;
    for (int i = 0; i < n; i++) {
        int k = ops[i] - OP_MISSION_1, s = i == m.sel[m.level], locked = k >= unlocked, done = k + 1 < unlocked;
        float x = 64 + i * (cw + gap), cx = x + cw / 2;
        add_hit(x, y, cw, ch, HIT_ITEM, i, 0);
        fill(x, y, cw, ch, locked ? C_PANEL_DIM : C_PANEL, 245);
        fill(x, y, cw, art, C_INSET, 255);
        fill(x, y + art, cw, 1, C_LINE, 255);
        char b[16];
        snprintf(b, sizeof b, "%d", k + 1);
        if (locked) lock_icon(cx, y + art / 2, 46, C_DIM);
        else text_c(UI_STENCIL, 84, 0, cx, y + art / 2, s ? C_ACCENT : C_TEXT, b);
        text(UI_SEMIBOLD, 13, 2, x + 16, y + art + 26, C_MUTED, up(T("MISSION")));
        text(UI_STENCIL, 30, 0, x + 16, y + art + 58, locked ? C_DIM : s ? C_ACCENT : C_TEXT, b);
        const char *st = locked ? "Locked" : done ? "Completed" : "Available";
        text(UI_SEMIBOLD, 15, 0, x + 16, y + art + 90, locked ? C_DIM : done ? C_DONE : C_TEXT, T(st));
        if (s) outline(x - 2, y - 2, cw + 4, ch + 4, 3, C_ACCENT);
        else outline(x, y, cw, ch, 1, C_LINE);
    }
    /* the selected mission */
    int k = n ? ops[m.sel[m.level]] - OP_MISSION_1 : 0;
    float by = y + ch + 30, bh = 96;
    fill(64, by, VW - 128, bh, C_PANEL, 240);
    outline(64, by, VW - 128, bh, 1, C_LINE);
    text(UI_SEMIBOLD, 12, 2, 88, by + 30, C_MUTED, up(T("Selected")));
    text(UI_STENCIL, 26, 1, 88, by + 62, C_ACCENT, up(label(OP_MISSION_1 + k)));
    float cx = 330;
    if (records_supported()) {
        char sc[32] = "-", tm[32] = "-";
        if (records_mission_has(k + 1)) {
            snprintf(sc, sizeof sc, "%u", (unsigned)records_mission_score(k + 1));
            records_time_text(records_mission_frames(k + 1), tm, sizeof tm);
        }
        fill(cx, by + 16, 1, bh - 32, C_LINE, 255);
        text(UI_SEMIBOLD, 12, 2, cx + 24, by + 30, C_MUTED, up(T("Best score")));
        text(UI_BOLD, 22, 0, cx + 24, by + 62, C_TEXT, sc);
        cx += 200;
        fill(cx, by + 16, 1, bh - 32, C_LINE, 255);
        text(UI_SEMIBOLD, 12, 2, cx + 24, by + 30, C_MUTED, up(T("Best time")));
        text(UI_BOLD, 22, 0, cx + 24, by + 62, C_TEXT, tm);
        cx += 190;
    }
    if (VW - 64 - cx - 48 < 240) return;                    /* no room for the note */
    fill(cx, by + 16, 1, bh - 32, C_LINE, 255);
    wrap(UI_REGULAR, 16, cx + 24, by + 24, VW - 64 - cx - 48, 24, C_BODY,
         T(k >= unlocked ? "Complete the previous mission to unlock it."
                         : "When you finish it, the game goes on with the next mission."));
}

void menu_draw(SDL_Renderer *r, int w, int h)
{
    if (m.level < 0) return;
    R = r;
    /* 720 units tall, at least 960 wide */
    float k = h / 720.0f;
    if (w / k < 960) k = w / 960.0f;
    if (k <= 0) return;
    K = k;
    n_hits = 0;
    SDL_SetRenderScale(r, k, k);
    VW = w / k;
    VH = h / k;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    const Page *p = m.stack[m.level];
    int at_start = p == &P_START;
    fill(0, 0, VW, VH, C_GROUND, at_start ? 150 : 215);

    if (m.capture >= 0 && on_gamepad_tab() && SDL_GetTicks() > m.capture_until) {
        fe_notify(T("Unchanged"));                    /* no button within the wait */
        capture_done();
    }
    switch (p->layout) {
    case L_HOME: draw_home(p); break;
    case L_MODES: draw_modes(p); break;
    case L_MISSIONS: draw_missions(p); break;
    default: draw_list(p); break;
    }
    /* key hints */
    int ops[32], n = page_ops(p, ops), cur = sel_op(ops, n);
    char v[64] = "";
    if (cur >= 0) value(cur, v, sizeof v);
    Hint hs[5];
    int nh = 0;
    if (p->layout == L_MODES || p->layout == L_MISSIONS) {
        hs[nh++] = (Hint){"A", "ENTER", "Start", 4};
        if (cur == OP_ARCADE) hs[nh++] = (Hint){"^v", "^v", "Continues", 0};
        hs[nh++] = (Hint){"B", "ESC", "Back", 5};
    } else if (m.capture >= 0) {
        hs[nh++] = (Hint){"ESC", "ESC", "Cancel", 5};
        if (!on_gamepad_tab()) hs[nh++] = (Hint){"DEL", "DEL", "Clear", 6};
    } else if (cur >= 0 && is_bind(cur)) {
        hs[nh++] = (Hint){"A", "ENTER", "Change", 4};
        hs[nh++] = (Hint){"Y", "DEL", "Clear", 6};
        hs[nh++] = (Hint){"LB RB", "Q E", "Tab", 10};
        hs[nh++] = (Hint){"B", "ESC", "Back", 5};
    } else {
        hs[nh++] = (Hint){"A", "ENTER", "Select", 4};
        if (v[0] && cur >= 0 && !is_result(cur)) hs[nh++] = (Hint){"‹ ›", "‹ ›", "Change", 3};
        if (tab_of(p) >= 0) hs[nh++] = (Hint){"LB RB", "Q E", "Tab", 10};
        if (!at_start) hs[nh++] = (Hint){"B", "ESC", p == &P_PAUSE ? "Back to the game" : "Back", 5};
    }
    footer(hs, nh);
    SDL_SetRenderScale(r, 1, 1);
}

/* ------------------------------------------------------------------ mouse */
/* (x, y): output pixels, as the renderer draws them (window.c converts the events). */
static const Hit *hit_at(float x, float y)
{
    SDL_FPoint pt = {x / K, y / K};
    for (int i = n_hits - 1; i >= 0; i--)          /* the last drawn (arrows) first */
        if (SDL_PointInRectFloat(&pt, &hits[i].r)) return &hits[i];
    return NULL;
}

void menu_mouse_move(float x, float y)
{
    if (m.level < 0 || m.capture >= 0) return;
    const Hit *h = hit_at(x, y);
    if (h && (h->kind == HIT_ITEM || h->kind == HIT_ARROW)) m.sel[m.level] = h->index;
}

void menu_mouse_click(float x, float y)
{
    if (m.level < 0 || m.capture >= 0) return;
    const Hit *h = hit_at(x, y);
    if (!h) return;
    const Page *p = m.stack[m.level];
    int op[32], n = page_ops(p, op);
    switch (h->kind) {
    case HIT_ITEM:
        if (h->index < n) { m.sel[m.level] = h->index; do_action(op[h->index], 0); }
        break;
    case HIT_ARROW:
        if (h->index < n) { m.sel[m.level] = h->index; do_action(op[h->index], h->d); }
        break;
    case HIT_TAB:
        last_tab = h->index;
        m.stack[m.level] = SETTINGS_TABS[last_tab];
        m.sel[m.level] = 0;
        break;
    default: menu_navigate(h->index); break;
    }
}

void menu_mouse_wheel(float dy)
{
    if (m.level < 0 || m.capture >= 0 || !dy) return;
    int cards = m.stack[m.level]->layout == L_MODES || m.stack[m.level]->layout == L_MISSIONS;
    menu_navigate(dy > 0 ? (cards ? 2 : 0) : (cards ? 3 : 1));
}
