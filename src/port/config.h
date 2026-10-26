/* User settings (<game id>.ini in the SDL preferences folder). */
#ifndef CONFIG_H
#define CONFIG_H

#include <SDL3/SDL.h>

enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_A, ACT_B, ACT_C, ACT_D, ACT_START, ACT_COIN,
    ACT_AUTO,                            /* autofire (its own button) */
    ACT_COUNT
};

/* Gamepad inputs: an SDL_GamepadButton, or a trigger or a stick direction taken as a button
   (past half way); PAD_NONE = unbound. Arcade sticks often report their lever as a stick. */
enum {
    PAD_NONE = -1, PAD_LT = SDL_GAMEPAD_BUTTON_COUNT, PAD_RT,
    PAD_LS_UP, PAD_LS_DOWN, PAD_LS_LEFT, PAD_LS_RIGHT, PAD_RS_UP, PAD_RS_DOWN, PAD_RS_LEFT, PAD_RS_RIGHT,
    PAD_INPUTS
};

/* The port's shortcuts, common to both players: a key and a gamepad input each. */
enum {
    HK_MENU, HK_PAUSE, HK_SAVE, HK_LOAD, HK_SLOT_PREV, HK_SLOT_NEXT, HK_FAST, HK_SCREENSHOT,
    HK_FULLSCREEN, HK_DEBUG, HK_SERVICE, HK_DEV_MENU,
    HK_COUNT
};

/* What a player's gamepad looks like (the buttons' names and the menu's picture). */
enum { PAD_STYLE_AUTO, PAD_STYLE_XBOX, PAD_STYLE_PLAYSTATION, PAD_STYLE_NINTENDO, PAD_STYLE_ARCADE, PAD_STYLE_COUNT };

enum { ASPECT_4_3, ASPECT_SQUARE, ASPECT_COUNT };
enum { FILTER_SHARP, FILTER_SMOOTH, FILTER_PIXEL, FILTER_COUNT };
/* Autofire: off, on its own button (ACT_AUTO) or on the fire button itself (A). */
enum { AUTO_OFF, AUTO_BUTTON, AUTO_FIRE, AUTO_COUNT };
#define AUTO_SPEEDS 3                   /* 10 (the pistol's maximum), 7.5 and 5 per second */
extern const int AUTO_PERIOD[AUTO_SPEEDS];  /* frames per press: 6, 8, 12 */

/* A gamepad profile: a mapping and a type, linked to the gamepad (SDL GUID) it was last chosen with. */
#define PAD_PROFILES 4
typedef struct {
    int button[ACT_COUNT];               /* PAD_* */
    int style;                           /* PAD_STYLE_* */
    char guid[40];                       /* "" = not linked */
} PadProfile;

typedef struct {
    /* video */
    int scale;                          /* initial window size (x 224 lines) */
    int fullscreen;
    int aspect;                         /* ASPECT_*: 4:3 like the monitor, or square pixels */
    int filter;                          /* FILTER_* */
    int scanlines;
    int vsync;                           /* vertical sync with the monitor */
    int integer_scale;                   /* the game image at a whole multiple of 224 lines */
    int crt;                             /* CRT_* (crt.h): off, Arcade, TV, Soft */
    int crt_curve;                       /* the CRT effect's screen curvature */
    /* audio */
    int volume;                         /* 0-100 */
    /* game */
    int turbo;                           /* 0 = like the original; 1 = 68000 at 2x (no slowdown) */
    int slot;                          /* active save-state slot, 1-9 */
    char roms[1024];                     /* remembered resources\game folder */
    int autofire[2];                  /* AUTO_* per player */
    int auto_speed;                  /* 0-2: 10, 7.5 or 5 per second */
    int debug;                           /* debug panel visible */
    int devmode;                         /* developer mode (cheats.h): the game's hidden menu */
    int rumble;                       /* strength 0-100 (0 = off) */
    int start_screen;                 /* show the start menu when the game opens */
    int blood;                          /* 0 = as set in the service menu; 1 = always red */
    int language;                        /* LANG_* (lang.h): frontend language */
    int focus_pause;                     /* pause while the window is not active */
    int fast_boot;                       /* run the BIOS boot unseen, as fast as possible */
    int boss_bar;                        /* big enemies' health bar (bossbar.c) */
    int runahead;                        /* frames emulated ahead (runahead.c): 0 = off */
    int pad_hides_cursor;                /* the gamepad hides the mouse cursor until the mouse or a key is used */
    /* controls: [player][action] */
    SDL_Scancode key[2][ACT_COUNT];
    int button[2][ACT_COUNT];            /* PAD_* */
    /* shortcuts */
    SDL_Scancode hk_key[HK_COUNT];
    int hk_pad[HK_COUNT];                /* PAD_* */
    char pad_guid[2][40];                /* the gamepad each player uses (SDL GUID); "" = in connection order */
    int pad_style[2];                    /* PAD_STYLE_* */
    /* gamepad profiles: button[] / pad_style[] are the working copy of each player's profile */
    PadProfile profile[PAD_PROFILES];
    int pad_profile[2];                  /* each player's profile (0-3) */
} Config;

extern const char *const ACTION_KEY[ACT_COUNT];   /* file keys: "up", "a"... */
extern const char *const ACTION_LABEL[ACT_COUNT];    /* for the menu */
extern const char *const HOTKEY_KEY[HK_COUNT];    /* file keys: "menu", "quick_save"... */
extern const char *const HOTKEY_LABEL[HK_COUNT];     /* for the menu */

/* A gamepad input's name in the file (SDL's: "a", "start", "lefttrigger"...), or NULL; and back
   (PAD_NONE if unknown). */
const char *pad_input_name(int in);
int pad_input_from_name(const char *s);

void config_defaults(Config *c);
/* A player's profile into their working copy (button[], pad_style[]), and back (the other player
   on the same profile gets it too); the profile linked to a gamepad GUID (-1 if none). */
void config_profile_load(Config *c, int player);
void config_profile_store(Config *c, int player);
int config_profile_of(const Config *c, const char *guid);
/* Reads the file over the current values (missing keys keep their value). */
int config_load(Config *c, const char *path);
int config_save(const Config *c, const char *path);

#endif
