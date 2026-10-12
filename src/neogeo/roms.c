/* Loading of the user's copy ("bring your own ROM").
 *
 * The files of the GOG/DotEmu installation (resources\game\mslugx_*) are read and validated
 * by SHA-256 against the values in docs/ROM_MAP.md. The repo only contains the hashes, never
 * the data. */
#include "neogeo.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *name;
    uint32_t size;
    const char *sha256;
} RomFile;

static const RomFile ROM_FILES[] = {
    {"mslugx_game_m68k", 5242880, "5313f40dd217140b27f033f95b90f8119f4ed4e6401533d699f1df874b14304b"},
    {"mslugx_bios_m68k", 131072, "01ff1db85c5b209d4f5879ea8348e3b39a237532c37232f14c2b48945e9aa7d8"},
    {"mslugx_game_sfix", 131072, "d8acb320a8d40424f67fff7e6181970772294a2f58ce8bc551cefb0427256b06"},
    {"mslugx_bios_sfix", 131072, "5e8dcbdfdb29b8a273a2bf40582946a84067975e98955d2b7b91c39f82a1fa0e"},
    {"mslugx_tiles", 50331648, "0945f34d952a4dfed664122440d032059eb49bf2a30a09351a395d77e5cfcd83"},
    {"mslugx_zoom_table", 131072, "2a76f023dc578804c6bcd58ed514ffc04fcb433b3bd75d4452575ffda57a0707"},
    {"mslugx_game_z80", 131072, "6a132323fa9f41ae0712644cc804e113257baf04b270c63f93dbd5193f02ffad"},
    {"mslugx_adpcm", 10485760, "b45ca27aa08aecec6f76bdc60e6066e72d80c6a37df18d3d62b6e06d6d1dc40c"},
};
#define N_ROM_FILES (int)(sizeof ROM_FILES / sizeof ROM_FILES[0])

static uint8_t *read_file(const char *dir, const RomFile *a, char *error, int error_size)
{
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, a->name);
    FILE *f = fopen(path, "rb");
    if (!f) { snprintf(error, error_size, "%s not found", path); return NULL; }
    uint8_t *d = malloc(a->size);
    size_t n = d ? fread(d, 1, a->size, f) : 0;
    int extra = fgetc(f) != EOF;
    fclose(f);
    if (n != a->size || extra) {
        free(d);
        snprintf(error, error_size, "%s does not have the expected size (%u bytes)", path, a->size);
        return NULL;
    }
    uint8_t h[32];
    char hex[65];
    sha256(d, a->size, h);
    for (int i = 0; i < 32; i++) snprintf(hex + 2 * i, 3, "%02x", h[i]);
    if (strcmp(hex, a->sha256) != 0) {
        free(d);
        snprintf(error, error_size, "%s does not match the GOG version (SHA-256 %s)", path, hex);
        return NULL;
    }
    return d;
}

static void swap_bytes(uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 2) { uint8_t t = d[i]; d[i] = d[i + 1]; d[i + 1] = t; }
}

int ng_roms_load(NgRoms *r, const char *dir, char *error, int error_size)
{
    uint8_t *d[N_ROM_FILES];
    memset(r, 0, sizeof *r);
    for (int i = 0; i < N_ROM_FILES; i++) {
        d[i] = read_file(dir, &ROM_FILES[i], error, error_size);
        if (!d[i]) {
            while (i--) free(d[i]);
            return -1;
        }
    }
    /* P and BIOS: same byte order as MAME's .p files; the 68000 view is the swapped one. */
    swap_bytes(d[0], ROM_FILES[0].size);
    swap_bytes(d[1], ROM_FILES[1].size);
    r->p = d[0]; r->p_size = ROM_FILES[0].size;
    r->bios = d[1];
    r->fix_game = d[2];
    r->fix_bios = d[3];
    r->tiles = d[4]; r->n_tiles = ROM_FILES[4].size / 128;
    r->zoom = d[5];
    r->z80 = d[6];
    r->adpcm = d[7]; r->adpcm_size = ROM_FILES[7].size;
    return 0;
}

void ng_roms_free(NgRoms *r)
{
    free(r->p); free(r->bios); free(r->fix_game); free(r->fix_bios); free(r->tiles); free(r->zoom);
    free(r->z80); free(r->adpcm);
    memset(r, 0, sizeof *r);
}
