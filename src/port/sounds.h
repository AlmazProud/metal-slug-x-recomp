/* Fase 6B/6C: menú de música, efectos y voces, y vibración del mando. */
#ifndef SOUNDS_H
#define SOUNDS_H

#include <SDL3/SDL.h>
#include <stdint.h>

/* Driver de sonido de Metal Slug X: un command de mode (00 música, 10 efectos, 11 voces) queda
 * active y el siguiente command se interpreta en ese mode. 03 reinicia el driver. */
enum { MODE_MUSIC = 0x00, MODE_EFFECTS = 0x10, MODE_VOICES = 0x11 };

typedef struct {
    uint8_t mode, command;
    uint16_t tenths;                    /* duración; 0xFFFF = en bucle */
    const char *name;
} Sound;

extern const Sound PROFILE_SOUNDS[];    /* profile_sounds.c (generado) */
extern const int PROFILE_SOUND_COUNT;

/* ------------------------------------------------------------------ menú de sonido */
/* window: para escribir nombres; ruta: archivo de nombres y vibración del usuario (sonidos.ini). */
void sounds_init(SDL_Window *window, const char *ruta);
void sounds_show(void);                /* guarda el estado del juego y detiene el 68000 */
void sounds_close(void);               /* restaura el juego tal como estaba */
int sounds_open(void);
void sounds_frame(void);               /* avanza el sonido un cuadro (en lugar del juego) */
/* Navegación: 0 arriba, 1 abajo, 2 izquierda, 3 derecha, 4 escuchar, 5 salir, 6 detener,
 * 7 renombrar, 8 cambiar la vibración del sonido. */
void sounds_navigate(int t);
void sounds_draw(SDL_Renderer *r, int w, int h);
/* Escritura de un name en curso. */
int sounds_editing(void);
void sounds_text(const char *utf8);
void sounds_edit_key(SDL_Scancode sc);

/* ------------------------------------------------------------------ vibración */
/* Mira los comandos de sonido del juego; la intensidad 0-100 (0 = apagada). */
void rumble_init(SDL_Gamepad **mandos, int n);
void rumble_strength(int intensidad);
void rumble_frame(void);             /* aplica lo pedido durante el cuadro */

#endif
