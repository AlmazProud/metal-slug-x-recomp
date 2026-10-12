/* Configuración del usuario (mslugx.ini en la carpeta de preferencias de SDL). */
#ifndef CONFIG_H
#define CONFIG_H

#include <SDL3/SDL.h>

enum {
    ACC_ARRIBA, ACC_ABAJO, ACC_IZQUIERDA, ACC_DERECHA,
    ACC_A, ACC_B, ACC_C, ACC_D, ACC_START, ACC_MONEDA,
    ACC_AUTO,                            /* disparo automático (botón propio) */
    ACC_N
};

enum { ASPECTO_4_3, ASPECTO_PIXELES, ASPECTO_N };
enum { FILTRO_NITIDO, FILTRO_SUAVE, FILTRO_PIXELES, FILTRO_N };
/* Disparo automático: no, con un botón propio (ACC_AUTO) o en el mismo botón de disparo (A). */
enum { AUTO_NO, AUTO_BOTON, AUTO_DISPARO, AUTO_N };
#define AUTO_VELOCIDADES 3                   /* 10 (máximo de la pistola), 7,5 y 5 por segundo */
extern const int AUTO_PERIODO[AUTO_VELOCIDADES];  /* cuadros por pulsación: 6, 8, 12 */

typedef struct {
    /* video */
    int escala;                          /* tamaño inicial de la ventana (x 224 líneas) */
    int pantalla_completa;
    int aspecto;                         /* ASPECTO_*: 4:3 como el monitor, o píxeles cuadrados */
    int filtro;                          /* FILTRO_* */
    int scanlines;
    /* audio */
    int volumen;                         /* 0-100 */
    /* juego */
    int turbo;                           /* 0 = como el original; 1 = 68000 al doble (sin ralentización) */
    int ranura;                          /* ranura de estado activa, 1-9 */
    char roms[1024];                     /* carpeta resources\game recordada */
    int autodisparo[2];                  /* AUTO_* por jugador */
    int auto_velocidad;                  /* 0-2: 10, 7,5 o 5 por segundo */
    int debug;                           /* panel de depuración visible */
    int vibracion;                       /* intensidad 0-100 (0 = apagada) */
    /* controles: [jugador][acción] */
    SDL_Scancode tecla[2][ACC_N];
    SDL_GamepadButton boton[2][ACC_N];
} Config;

extern const char *const ACCION_NOMBRE[ACC_N];   /* claves del archivo: "arriba", "a"... */
extern const char *const ACCION_TEXTO[ACC_N];    /* para el menú */

void config_por_defecto(Config *c);
/* Lee el archivo sobre los valores actuales (las claves que falten conservan su valor). */
int config_cargar(Config *c, const char *ruta);
int config_guardar(const Config *c, const char *ruta);

#endif
