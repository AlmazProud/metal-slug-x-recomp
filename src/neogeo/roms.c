/* Carga de la copia del usuario ("trae tu propio ROM").
 *
 * Se leen los archivos de la instalación de GOG/DotEmu (resources\game\mslugx_*) y se validan
 * por SHA-256 contra los valores de docs/ROM_MAP.md. El repo solo contiene los hashes, nunca
 * los datos. */
#include "neogeo.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *nombre;
    uint32_t tam;
    const char *sha256;
} Archivo;

static const Archivo ARCHIVOS[] = {
    {"mslugx_game_m68k", 5242880, "5313f40dd217140b27f033f95b90f8119f4ed4e6401533d699f1df874b14304b"},
    {"mslugx_bios_m68k", 131072, "01ff1db85c5b209d4f5879ea8348e3b39a237532c37232f14c2b48945e9aa7d8"},
    {"mslugx_game_sfix", 131072, "d8acb320a8d40424f67fff7e6181970772294a2f58ce8bc551cefb0427256b06"},
    {"mslugx_bios_sfix", 131072, "5e8dcbdfdb29b8a273a2bf40582946a84067975e98955d2b7b91c39f82a1fa0e"},
    {"mslugx_tiles", 50331648, "0945f34d952a4dfed664122440d032059eb49bf2a30a09351a395d77e5cfcd83"},
    {"mslugx_zoom_table", 131072, "2a76f023dc578804c6bcd58ed514ffc04fcb433b3bd75d4452575ffda57a0707"},
    {"mslugx_game_z80", 131072, "6a132323fa9f41ae0712644cc804e113257baf04b270c63f93dbd5193f02ffad"},
    {"mslugx_adpcm", 10485760, "b45ca27aa08aecec6f76bdc60e6066e72d80c6a37df18d3d62b6e06d6d1dc40c"},
};
#define N_ARCHIVOS (int)(sizeof ARCHIVOS / sizeof ARCHIVOS[0])

static uint8_t *leer(const char *carpeta, const Archivo *a, char *error, int tam_error)
{
    char ruta[1024];
    snprintf(ruta, sizeof ruta, "%s/%s", carpeta, a->nombre);
    FILE *f = fopen(ruta, "rb");
    if (!f) { snprintf(error, tam_error, "No se encontró %s", ruta); return NULL; }
    uint8_t *d = malloc(a->tam);
    size_t n = d ? fread(d, 1, a->tam, f) : 0;
    int extra = fgetc(f) != EOF;
    fclose(f);
    if (n != a->tam || extra) {
        free(d);
        snprintf(error, tam_error, "%s no tiene el tamaño esperado (%u bytes)", ruta, a->tam);
        return NULL;
    }
    uint8_t h[32];
    char hex[65];
    sha256(d, a->tam, h);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
    if (strcmp(hex, a->sha256) != 0) {
        free(d);
        snprintf(error, tam_error, "%s no coincide con la versión de GOG (SHA-256 %s)", ruta, hex);
        return NULL;
    }
    return d;
}

static void intercambiar_bytes(uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 2) { uint8_t t = d[i]; d[i] = d[i + 1]; d[i + 1] = t; }
}

int ng_roms_cargar(NgRoms *r, const char *carpeta, char *error, int tam_error)
{
    uint8_t *d[N_ARCHIVOS];
    memset(r, 0, sizeof *r);
    for (int i = 0; i < N_ARCHIVOS; i++) {
        d[i] = leer(carpeta, &ARCHIVOS[i], error, tam_error);
        if (!d[i]) {
            while (i--) free(d[i]);
            return -1;
        }
    }
    /* P y BIOS: mismo orden de bytes que los .p de MAME; la vista del 68000 es la intercambiada. */
    intercambiar_bytes(d[0], ARCHIVOS[0].tam);
    intercambiar_bytes(d[1], ARCHIVOS[1].tam);
    r->p = d[0]; r->p_tam = ARCHIVOS[0].tam;
    r->bios = d[1];
    r->fix_juego = d[2];
    r->fix_bios = d[3];
    r->tiles = d[4]; r->n_tiles = ARCHIVOS[4].tam / 128;
    r->zoom = d[5];
    r->z80 = d[6];
    r->adpcm = d[7]; r->adpcm_tam = ARCHIVOS[7].tam;
    return 0;
}

void ng_roms_liberar(NgRoms *r)
{
    free(r->p); free(r->bios); free(r->fix_juego); free(r->fix_bios); free(r->tiles); free(r->zoom);
    free(r->z80); free(r->adpcm);
    memset(r, 0, sizeof *r);
}
