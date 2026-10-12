/* Configuración del usuario (mslugx.ini en la carpeta de preferencias de SDL). */
#ifndef CONFIG_H
#define CONFIG_H

#include <SDL3/SDL.h>

enum {
    ACT_UP, ACT_DOWN, ACT_LEFT, ACT_RIGHT,
    ACT_A, ACT_B, ACT_C, ACT_D, ACT_START, ACT_COIN,
    ACT_AUTO,                            /* disparo automático (botón propio) */
    ACT_COUNT
};

enum { ASPECT_4_3, ASPECT_SQUARE, ASPECT_COUNT };
enum { FILTER_SHARP, FILTER_SMOOTH, FILTER_PIXEL, FILTER_COUNT };
/* Disparo automático: no, con un botón propio (ACT_AUTO) o en el mismo botón de disparo (A). */
enum { AUTO_OFF, AUTO_BUTTON, AUTO_FIRE, AUTO_COUNT };
#define AUTO_SPEEDS 3                   /* 10 (máximo de la pistola), 7,5 y 5 por second */
extern const int AUTO_PERIOD[AUTO_SPEEDS];  /* frames por pulsación: 6, 8, 12 */

typedef struct {
    /* video */
    int scale;                          /* tamaño inicial de la window (x 224 líneas) */
    int fullscreen;
    int aspect;                         /* ASPECTO_*: 4:3 como el monitor, o píxeles cuadrados */
    int filter;                          /* FILTRO_* */
    int scanlines;
    int vsync;                           /* sincronía vertical con el monitor */
    /* audio */
    int volume;                         /* 0-100 */
    /* juego */
    int turbo;                           /* 0 = como el original; 1 = 68000 al doble (sin ralentización) */
    int slot;                          /* slot de estado active, 1-9 */
    char roms[1024];                     /* carpeta resources\game recordada */
    int autofire[2];                  /* AUTO_* por jugador */
    int auto_speed;                  /* 0-2: 10, 7,5 o 5 por second */
    int debug;                           /* panel de depuración visible */
    int rumble;                       /* intensidad 0-100 (0 = apagada) */
    int start_screen;                 /* mostrar el menú de inicio al abrir */
    int blood;                          /* 0 = la del menú de service; 1 = siempre roja */
    /* controles: [jugador][acción] */
    SDL_Scancode key[2][ACT_COUNT];
    SDL_GamepadButton button[2][ACT_COUNT];
} Config;

extern const char *const ACTION_KEY[ACT_COUNT];   /* claves del archivo: "arriba", "a"... */
extern const char *const ACTION_LABEL[ACT_COUNT];    /* para el menú */

void config_defaults(Config *c);
/* Lee el archivo sobre los valores actuales (las claves que falten conservan su value). */
int config_load(Config *c, const char *ruta);
int config_save(const Config *c, const char *ruta);

#endif
