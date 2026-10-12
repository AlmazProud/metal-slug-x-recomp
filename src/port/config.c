/* mslugx.ini: formato INI sencillo ([sección], clave=valor, comentarios con ; o #).
 *
 * Las teclas usan los nombres de SDL (SDL_GetScancodeName: "Up", "Z", "Left Shift"...) y los
 * botones del mando los de SDL_GetGamepadStringForButton ("a", "b", "x", "y", "start",
 * "back", "dpup"..., con la disposición de Xbox: "a" es el botón de abajo). Una tecla o un
 * botón vacío deja la acción sin asignar.
 */
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *const ACCION_NOMBRE[ACC_N] = {
    "arriba", "abajo", "izquierda", "derecha", "a", "b", "c", "d", "start", "moneda", "auto",
};
const char *const ACCION_TEXTO[ACC_N] = {
    "Arriba", "Abajo", "Izquierda", "Derecha", "A (disparo)", "B (salto)", "C (granada)", "D",
    "Start", "Moneda", "Disparo automatico",
};

static const char *const ASPECTO_NOMBRE[ASPECTO_N] = {"4:3", "pixeles"};
static const char *const FILTRO_NOMBRE[FILTRO_N] = {"nitido", "suave", "pixeles"};
static const char *const AUTO_NOMBRE[AUTO_N] = {"no", "boton", "disparo"};
static const char *const VELOCIDAD_NOMBRE[AUTO_VELOCIDADES] = {"10", "7.5", "5"};
const int AUTO_PERIODO[AUTO_VELOCIDADES] = {6, 8, 12};

void config_por_defecto(Config *c)
{
    memset(c, 0, sizeof *c);
    c->escala = 3;
    c->aspecto = ASPECTO_4_3;
    c->filtro = FILTRO_NITIDO;
    c->volumen = 100;
    c->ranura = 1;
    c->autodisparo[0] = c->autodisparo[1] = AUTO_BOTON;
    static const SDL_Scancode t1[ACC_N] = {
        SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT,
        SDL_SCANCODE_Z, SDL_SCANCODE_X, SDL_SCANCODE_C, SDL_SCANCODE_V, SDL_SCANCODE_1, SDL_SCANCODE_5,
        SDL_SCANCODE_B,
    };
    static const SDL_Scancode t2[ACC_N] = {
        SDL_SCANCODE_I, SDL_SCANCODE_K, SDL_SCANCODE_J, SDL_SCANCODE_L,
        SDL_SCANCODE_A, SDL_SCANCODE_S, SDL_SCANCODE_D, SDL_SCANCODE_F, SDL_SCANCODE_2, SDL_SCANCODE_6,
        SDL_SCANCODE_G,
    };
    static const SDL_GamepadButton b[ACC_N] = {
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_SOUTH,
        SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    };
    for (int i = 0; i < ACC_N; i++) {
        c->tecla[0][i] = t1[i];
        c->tecla[1][i] = t2[i];
        c->boton[0][i] = c->boton[1][i] = b[i];
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

int config_cargar(Config *c, const char *ruta)
{
    FILE *f = fopen(ruta, "r");
    if (!f) return -1;
    char linea[1200], seccion[64] = "";
    while (fgets(linea, sizeof linea, f)) {
        char *l = recortar(linea);
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
            if (!strcmp(k, "escala")) c->escala = entero(v, 1, 10, c->escala);
            else if (!strcmp(k, "pantalla_completa")) c->pantalla_completa = entero(v, 0, 1, c->pantalla_completa);
            else if (!strcmp(k, "aspecto")) c->aspecto = buscar(ASPECTO_NOMBRE, ASPECTO_N, v, c->aspecto);
            else if (!strcmp(k, "filtro")) c->filtro = buscar(FILTRO_NOMBRE, FILTRO_N, v, c->filtro);
            else if (!strcmp(k, "scanlines")) c->scanlines = entero(v, 0, 1, c->scanlines);
        } else if (!strcmp(seccion, "audio")) {
            if (!strcmp(k, "volumen")) c->volumen = entero(v, 0, 100, c->volumen);
        } else if (!strcmp(seccion, "juego")) {
            if (!strcmp(k, "turbo")) c->turbo = entero(v, 0, 2, c->turbo);
            else if (!strcmp(k, "ranura")) c->ranura = entero(v, 1, 9, c->ranura);
            else if (!strcmp(k, "roms")) snprintf(c->roms, sizeof c->roms, "%s", v);
            else if (!strcmp(k, "autodisparo1")) c->autodisparo[0] = buscar(AUTO_NOMBRE, AUTO_N, v, c->autodisparo[0]);
            else if (!strcmp(k, "autodisparo2")) c->autodisparo[1] = buscar(AUTO_NOMBRE, AUTO_N, v, c->autodisparo[1]);
            else if (!strcmp(k, "autodisparo_por_segundo"))
                c->auto_velocidad = buscar(VELOCIDAD_NOMBRE, AUTO_VELOCIDADES, v, c->auto_velocidad);
            else if (!strcmp(k, "debug")) c->debug = entero(v, 0, 1, c->debug);
        } else if (!strncmp(seccion, "teclado", 7) || !strncmp(seccion, "mando", 5)) {
            int teclado = seccion[0] == 't';
            int j = seccion[teclado ? 7 : 5] - '1';
            if (j < 0 || j > 1) continue;
            int a = buscar(ACCION_NOMBRE, ACC_N, k, -1);
            if (a < 0) continue;
            if (teclado) c->tecla[j][a] = *v ? SDL_GetScancodeFromName(v) : SDL_SCANCODE_UNKNOWN;
            else c->boton[j][a] = *v ? SDL_GetGamepadButtonFromString(v) : SDL_GAMEPAD_BUTTON_INVALID;
        }
    }
    fclose(f);
    return 0;
}

int config_guardar(const Config *c, const char *ruta)
{
    FILE *f = fopen(ruta, "w");
    if (!f) return -1;
    fprintf(f, "; Metal Slug X (port nativo): configuración. Se reescribe al salir.\n\n");
    fprintf(f, "[video]\n; escala: tamaño inicial de la ventana (x 224 líneas)\nescala=%d\npantalla_completa=%d\n",
            c->escala, c->pantalla_completa);
    fprintf(f, "; aspecto: 4:3 (como el monitor original) o pixeles (cuadrados, 320x224)\naspecto=%s\n",
            ASPECTO_NOMBRE[c->aspecto]);
    fprintf(f, "; filtro: nitido (píxeles parejos con bordes suaves), suave o pixeles\nfiltro=%s\nscanlines=%d\n\n",
            FILTRO_NOMBRE[c->filtro], c->scanlines);
    fprintf(f, "[audio]\nvolumen=%d\n\n", c->volumen);
    fprintf(f, "[juego]\n; turbo: 0 = como el original (con la ralentización), 1 = 68000 al doble, 2 = al cuádruple\n");
    fprintf(f, "turbo=%d\nranura=%d\nroms=%s\n", c->turbo, c->ranura, c->roms);
    fprintf(f, "; autodisparo: no, boton (tecla o botón propio, \"auto\" en los controles) o disparo\n");
    fprintf(f, "autodisparo1=%s\nautodisparo2=%s\n", AUTO_NOMBRE[c->autodisparo[0]], AUTO_NOMBRE[c->autodisparo[1]]);
    fprintf(f, "; autodisparo_por_segundo: 10 (máximo de la pistola), 7.5 o 5\nautodisparo_por_segundo=%s\n",
            VELOCIDAD_NOMBRE[c->auto_velocidad]);
    fprintf(f, "; debug: panel de depuración (F3)\ndebug=%d\n", c->debug);
    for (int j = 0; j < 2; j++) {
        fprintf(f, "\n[teclado%d]\n", j + 1);
        for (int a = 0; a < ACC_N; a++)
            fprintf(f, "%s=%s\n", ACCION_NOMBRE[a],
                    c->tecla[j][a] != SDL_SCANCODE_UNKNOWN ? SDL_GetScancodeName(c->tecla[j][a]) : "");
        fprintf(f, "\n[mando%d]\n", j + 1);
        for (int a = 0; a < ACC_N; a++) {
            const char *n = c->boton[j][a] != SDL_GAMEPAD_BUTTON_INVALID ? SDL_GetGamepadStringForButton(c->boton[j][a]) : NULL;
            fprintf(f, "%s=%s\n", ACCION_NOMBRE[a], n ? n : "");
        }
    }
    fclose(f);
    return 0;
}
