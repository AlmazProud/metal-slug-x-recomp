/* User settings (mslugx.ini in the SDL preferences folder). */
#ifndef CONFIG_H
#define CONFIG_H

#include <SDL3/SDL.h>

enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_A, ACT_B, ACT_C, ACT_D, ACT_START, ACT_COIN,
    ACT_AUTO,                            /* autofire (its own button) */
    ACT_COUNT
};

enum { ASPECT_4_3, ASPECT_SQUARE, ASPECT_COUNT };
enum { FILTER_SHARP, FILTER_SMOOTH, FILTER_PIXEL, FILTER_COUNT };
/* Autofire: off, on its own button (ACT_AUTO) or on the fire button itself (A). */
enum { AUTO_OFF, AUTO_BUTTON, AUTO_FIRE, AUTO_COUNT };
#define AUTO_SPEEDS 3                   /* 10 (the pistol's maximum), 7.5 and 5 per second */
extern const int AUTO_PERIOD[AUTO_SPEEDS];  /* frames per press: 6, 8, 12 */

typedef struct {
    /* video */
    int scale;                          /* initial window size (x 224 lines) */
    int fullscreen;
    int aspect;                         /* ASPECT_*: 4:3 like the monitor, or square pixels */
    int filter;                          /* FILTER_* */
    int scanlines;
    int vsync;                           /* vertical sync with the monitor */
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
    /* controls: [player][action] */
    SDL_Scancode key[2][ACT_COUNT];
    SDL_GamepadButton button[2][ACT_COUNT];
} Config;

extern const char *const ACTION_KEY[ACT_COUNT];   /* file keys: "up", "a"... */
extern const char *const ACTION_LABEL[ACT_COUNT];    /* for the menu */

void config_defaults(Config *c);
/* Reads the file over the current values (missing keys keep their value). */
int config_load(Config *c, const char *path);
int config_save(const Config *c, const char *path);

#endif
