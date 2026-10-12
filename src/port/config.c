/* mslugx.ini: formato INI sencillo ([sección], clave=value, comentarios con ; o #).
 *
 * Las teclas usan los nombres de SDL (SDL_GetScancodeName: "Up", "Z", "Left Shift"...) y los
 * botones del mando los de SDL_GetGamepadStringForButton ("a", "b", "x", "y", "start",
 * "back", "dpup"..., con la disposición de Xbox: "a" es el botón de abajo). Una key o un
 * botón vacío deja la acción sin asignar.
 */
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const ACTION_KEY[ACT_COUNT] = {
    "arriba", "abajo", "izquierda", "derecha", "a", "b", "c", "d", "start", "moneda", "auto",
};
const char *const ACTION_LABEL[ACT_COUNT] = {
    "Arriba", "Abajo", "Izquierda", "Derecha", "A (disparo)", "B (salto)", "C (granada)", "D",
    "Start", "Moneda", "Disparo automatico",
};

static const char *const ASPECTO_NOMBRE[ASPECT_COUNT] = {"4:3", "pixeles"};
static const char *const FILTRO_NOMBRE[FILTER_COUNT] = {"nitido", "suave", "pixeles"};
static const char *const AUTO_NOMBRE[AUTO_COUNT] = {"no", "button", "disparo"};
static const char *const VELOCIDAD_NOMBRE[AUTO_SPEEDS] = {"10", "7.5", "5"};
const int AUTO_PERIOD[AUTO_SPEEDS] = {6, 8, 12};

void config_defaults(Config *c)
{
    memset(c, 0, sizeof *c);
    c->scale = 3;
    c->aspect = ASPECT_4_3;
    c->filter = FILTER_SHARP;
    c->volume = 100;
    c->slot = 1;
    c->autofire[0] = c->autofire[1] = AUTO_BUTTON;
    c->rumble = 70;
    c->vsync = 1;
    c->start_screen = 1;
    static const SDL_Scancode t1[ACT_COUNT] = {
        SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_1, SDL_SCANCODE_5,
        SDL_SCANCODE_B,
    };
    static const SDL_Scancode t2[ACT_COUNT] = {
        SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L,
        SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_2, SDL_SCANCODE_6,
        SDL_SCANCODE_G,
    };
    static const SDL_GamepadButton b[ACT_COUNT] = {
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_SOUTH,
        SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    };
    for (int i = 0; i < ACT_COUNT; i++) {
        c->key[0][i] = t1[i];
        c->key[1][i] = t2[i];
        c->button[0][i] = c->button[1][i] = b[i];
    }
}

static char *recortar(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *f = s + strlen(s);
    while (f > s && isspace((unsigned char)f[-1])) *--f = 0;
    return s;
}

static int buscar(const char *const *nombres, int n, const char *v, int actual)
{
    for (int i = 0; i < n; i++)
        if (!SDL_strcasecmp(nombres[i], v)) return i;
    return actual;
}

static int entero(const char *v, int min, int max, int actual)
{
    char *fin;
    long n = strtol(v, &fin, 10);
    if (fin == v) return actual;
    return n < min ? min : n > max ? max : (int)n;
}

int config_load(Config *c, const char *ruta)
{
    FILE *f = fopen(ruta, "r");
    if (!f) return -1;
    char line[1200], seccion[64] = "";
    while (fgets(line, sizeof line, f)) {
        char *l = recortar(line);
        if (!*l || *l == ';' || *l == '#') continue;
        if (*l == '[') {
            char *fin = strchr(l, ']');
            if (fin) { *fin = 0; snprintf(seccion, sizeof seccion, "%s", recortar(l + 1)); }
            continue;
        }
        char *igual = strchr(l, '=');
        if (!igual) continue;
        *igual = 0;
        char *k = recortar(l), *v = recortar(igual + 1);
        if (!strcmp(seccion, "video")) {
            if (!strcmp(k, "scale")) c->scale = entero(v, 1, 10, c->scale);
            else if (!strcmp(k, "fullscreen")) c->fullscreen = entero(v, 0, 1, c->fullscreen);
            else if (!strcmp(k, "aspect")) c->aspect = buscar(ASPECTO_NOMBRE, ASPECT_COUNT, v, c->aspect);
            else if (!strcmp(k, "filter")) c->filter = buscar(FILTRO_NOMBRE, FILTER_COUNT, v, c->filter);
            else if (!strcmp(k, "scanlines")) c->scanlines = entero(v, 0, 1, c->scanlines);
            else if (!strcmp(k, "vsync")) c->vsync = entero(v, 0, 1, c->vsync);
        } else if (!strcmp(seccion, "audio")) {
            if (!strcmp(k, "volume")) c->volume = entero(v, 0, 100, c->volume);
        } else if (!strcmp(seccion, "juego")) {
            if (!strcmp(k, "turbo")) c->turbo = entero(v, 0, 2, c->turbo);
            else if (!strcmp(k, "slot")) c->slot = entero(v, 1, 9, c->slot);
            else if (!strcmp(k, "roms")) snprintf(c->roms, sizeof c->roms, "%s", v);
            else if (!strcmp(k, "autodisparo1")) c->autofire[0] = buscar(AUTO_NOMBRE, AUTO_COUNT, v, c->autofire[0]);
            else if (!strcmp(k, "autodisparo2")) c->autofire[1] = buscar(AUTO_NOMBRE, AUTO_COUNT, v, c->autofire[1]);
            else if (!strcmp(k, "autodisparo_por_segundo"))
                c->auto_speed = buscar(VELOCIDAD_NOMBRE, AUTO_SPEEDS, v, c->auto_speed);
            else if (!strcmp(k, "debug")) c->debug = entero(v, 0, 1, c->debug);
            else if (!strcmp(k, "rumble")) c->rumble = entero(v, 0, 100, c->rumble);
            else if (!strcmp(k, "start_screen")) c->start_screen = entero(v, 0, 1, c->start_screen);
            else if (!strcmp(k, "blood")) c->blood = entero(v, 0, 1, c->blood);
        } else if (!strncmp(seccion, "teclado", 7) || !strncmp(seccion, "mando", 5)) {
            int teclado = seccion[0] == 't';
            int j = seccion[teclado ? 7 : 5] - '1';
            if (j < 0 || j > 1) continue;
            int a = buscar(ACTION_KEY, ACT_COUNT, k, -1);
            if (a < 0) continue;
            if (teclado) c->key[j][a] = *v ? SDL_GetScancodeFromName(v) : SDL_SCANCODE_UNKNOWN;
            else c->button[j][a] = *v ? SDL_GetGamepadButtonFromString(v) : SDL_GAMEPAD_BUTTON_INVALID;
        }
    }
    fclose(f);
    return 0;
}

int config_save(const Config *c, const char *ruta)
{
    FILE *f = fopen(ruta, "w");
    if (!f) return -1;
    fprintf(f, "; Metal Slug X (port nativo): configuración. Se reescribe al salir.\n\n");
    fprintf(f, "[video]\n; scale: tamaño inicial de la window (x 224 líneas)\nescala=%d\npantalla_completa=%d\n",
            c->scale, c->fullscreen);
    fprintf(f, "; aspect: 4:3 (como el monitor original) o pixeles (cuadrados, 320x224)\naspecto=%s\n",
            ASPECTO_NOMBRE[c->aspect]);
    fprintf(f, "; filter: nitido (píxeles parejos con bordes suaves), suave o pixeles\nfiltro=%s\nscanlines=%d\n\n",
            FILTRO_NOMBRE[c->filter], c->scanlines);
    fprintf(f, "; vsync: 1 = sincronía con el monitor (sin cortes en la imagen), 0 = ritmo exacto de la placa\n"
               "vsync=%d\n\n", c->vsync);
    fprintf(f, "[audio]\nvolumen=%d\n\n", c->volume);
    fprintf(f, "[juego]\n; turbo: 0 = como el original (con la ralentización), 1 = 68000 al doble, 2 = al cuádruple\n");
    fprintf(f, "turbo=%d\nranura=%d\nroms=%s\n", c->turbo, c->slot, c->roms);
    fprintf(f, "; autofire: no, button (key o botón propio, \"auto\" en los controles) o disparo\n");
    fprintf(f, "autodisparo1=%s\nautodisparo2=%s\n", AUTO_NOMBRE[c->autofire[0]], AUTO_NOMBRE[c->autofire[1]]);
    fprintf(f, "; autodisparo_por_segundo: 10 (máximo de la pistola), 7.5 o 5\nautodisparo_por_segundo=%s\n",
            VELOCIDAD_NOMBRE[c->auto_speed]);
    fprintf(f, "; debug: panel de depuración (F3)\ndebug=%d\n", c->debug);
    fprintf(f, "; rumble: intensidad del mando con explosiones y disparos, 0-100 (0 = apagada)\nvibracion=%d\n",
            c->rumble);
    fprintf(f, "; start_screen: menú de inicio al abrir el juego (1) o directo al juego (0)\npantalla_inicio=%d\n",
            c->start_screen);
    fprintf(f, "; blood: 0 = la que diga el menú de service (BLOOD, en EE. UU. viene apagada), 1 = siempre roja\n"
               "blood=%d\n", c->blood);
    for (int j = 0; j < 2; j++) {
        fprintf(f, "\n[teclado%d]\n", j + 1);
        for (int a = 0; a < ACT_COUNT; a++)
            fprintf(f, "%s=%s\n", ACTION_KEY[a],
                    c->key[j][a] != SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeName(c->key[j][a]) : "");
        fprintf(f, "\n[mando%d]\n", j + 1);
        for (int a = 0; a < ACT_COUNT; a++) {
            const char *n = c->button[j][a] != SDL_GAMEPAD_BUTTON_INVALID ? SDL_GetGamepadStringForButton(c->button[j][a]) : NULL;
            fprintf(f, "%s=%s\n", ACTION_KEY[a], n ? n : "");
        }
    }
    fclose(f);
    return 0;
}
