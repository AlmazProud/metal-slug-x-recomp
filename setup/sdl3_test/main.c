/* Environment test: opens an SDL3 window for a few milliseconds and exits on its own.
 * Usage: hello_sdl3 [milliseconds]   (default 2000) */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[])
{
    Uint64 duration_ms = 2000;
    if (argc > 1) {
        duration_ms = (Uint64)strtoul(argv[1], NULL, 10);
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window *window = NULL;
    SDL_Renderer *render = NULL;
    if (!SDL_CreateWindowAndRenderer("Hello - Projecto Decompile", 640, 448, 0, &window, &render)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    int v = SDL_GetVersion();
    printf("SDL %d.%d.%d, renderer %s\n", SDL_VERSIONNUM_MAJOR(v), SDL_VERSIONNUM_MINOR(v),
           SDL_VERSIONNUM_MICRO(v), SDL_GetRendererName(render));

    Uint64 start = SDL_GetTicks();
    bool active = true;
    while (active && SDL_GetTicks() - start < duration_ms) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_QUIT) {
                active = false;
            }
        }
        float t = (float)(SDL_GetTicks() - start) / (float)duration_ms;
        SDL_SetRenderDrawColorFloat(render, 0.10f, 0.15f + 0.6f * t, 0.35f, 1.0f);
        SDL_RenderClear(render);
        SDL_RenderPresent(render);
        SDL_Delay(16);
    }

    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("OK");
    return 0;
}
