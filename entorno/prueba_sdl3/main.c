/* Prueba del entorno: abre una ventana SDL3 unos milisegundos y termina sola.
 * Uso: hola_sdl3 [milisegundos]   (por defecto 2000) */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    Uint64 duracion = 2000;
    if (argc > 1) {
        duracion = (Uint64)strtoul(argv[1], NULL, 10);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *ventana = NULL;
    SDL_Renderer *render = NULL;
    if (!SDL_CreateWindowAndRenderer("Hola - Projecto Decompile", 640, 448, 0, &ventana, &render)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    int v = SDL_GetVersion();
    printf("SDL %d.%d.%d, renderer %s\n", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
           SDL_VERSIONNUM_MICRO(v), SDL_GetRendererName(render));

    Uint64 inicio = SDL_GetTicks();
    bool activo = true;
    while (activo && SDL_GetTicks() - inicio < duracion) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                activo = false;
            }
        }
        float t = (float)(SDL_GetTicks() - inicio) / (float)duracion;
        SDL_SetRenderDrawColorFloat(render, 0.10f, 0.15f + 0.6f * t, 0.35f, 1.0f);
        SDL_RenderClear(render);
        SDL_RenderPresent(render);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(ventana);
    SDL_Quit();
    puts("OK");
    return 0;
}
