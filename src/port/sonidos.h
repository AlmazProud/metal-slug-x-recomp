/* Fase 6B/6C: menú de música, efectos y voces, y vibración del mando. */
#ifndef SONIDOS_H
#define SONIDOS_H

#include <SDL3/SDL.h>
#include <stdint.h>

/* Driver de sonido de Metal Slug X: un comando de modo (00 música, 10 efectos, 11 voces) queda
 * activo y el siguiente comando se interpreta en ese modo. 03 reinicia el driver. */
enum { MODO_MUSICA = 0x00, MODO_EFECTOS = 0x10, MODO_VOCES = 0x11 };

typedef struct {
    uint8_t modo, comando;
    uint16_t decimas;                    /* duración; 0xFFFF = en bucle */
    const char *nombre;
} Sonido;

extern const Sonido PERFIL_SONIDOS[];    /* perfil_sonidos.c (generado) */
extern const int PERFIL_N_SONIDOS;

/* ------------------------------------------------------------------ menú de sonido */
/* ventana: para escribir nombres; ruta: archivo de nombres y vibración del usuario (sonidos.ini). */
void sonidos_iniciar(SDL_Window *ventana, const char *ruta);
void sonidos_abrir(void);                /* guarda el estado del juego y detiene el 68000 */
void sonidos_cerrar(void);               /* restaura el juego tal como estaba */
int sonidos_abierto(void);
void sonidos_cuadro(void);               /* avanza el sonido un cuadro (en lugar del juego) */
/* Navegación: 0 arriba, 1 abajo, 2 izquierda, 3 derecha, 4 escuchar, 5 salir, 6 detener,
 * 7 renombrar, 8 cambiar la vibración del sonido. */
void sonidos_navegar(int t);
void sonidos_dibujar(SDL_Renderer *r, int w, int h);
/* Escritura de un nombre en curso. */
int sonidos_editando(void);
void sonidos_texto(const char *utf8);
void sonidos_tecla_edicion(SDL_Scancode sc);

/* ------------------------------------------------------------------ vibración */
/* Mira los comandos de sonido del juego; la intensidad 0-100 (0 = apagada). */
void vibracion_iniciar(SDL_Gamepad **mandos, int n);
void vibracion_intensidad(int intensidad);
void vibracion_cuadro(void);             /* aplica lo pedido durante el cuadro */

#endif
