/* Loading of the user's copy ("bring your own ROM") of ng_game.
 *
 * `path` can be a folder (searched up to a few levels deep) or a zip. Two layouts are
 * recognized:
 *
 *  - DotEmu files (the PC releases: GOG, and most likely Steam): resources\game\<id>_*.
 *    Validated by SHA-256 against the GOG copy (ng_game->dotemu, docs/ROM_MAP.md). Files of the
 *    right size with another hash still load, marked as an unverified version.
 *  - Code Mystics files (Metal Slug 4's GOG release: Data\rom\p1.bin...), by name and size
 *    (ng_game->codemystics), checked by SHA-256, with the Neo Geo system files of a MAME BIOS
 *    set found below the path.
 *  - A MAME set (an arcade board dump): <id> + neogeo, as zips or loose files. Each file is
 *    recognized by its CRC-32 and confirmed by SHA-1, the hashes MAME publishes (mame -listxml),
 *    whatever its name; the graphics are converted to the DotEmu layout the runtime uses (the
 *    inverse of tools/extract_roms.py). Only for games whose descriptor lists its files
 *    (ng_game->mame); encrypted sets need the game's decryption (ng_game->crypt). Without it
 *    (ng_game->mame_encrypted) the set is recognized and reported, not loaded.
 *
 * The repo only contains hashes, never data. */
#include "neogeo.h"
#include "hashes.h"
#include "sha256.h"
#include "unzip.h"

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MB (1u << 20)

/* ------------------------------------------------------------------ catalog */
/* System ROMs of the MAME set (neogeo), from `mame -listxml` (MAME 0.289). The cartridge files
   come from the game descriptor (ng_game->mame). BIOSes in order of preference: the US MVS BIOS
   first (it is the GOG copies' BIOS, the one the port was verified with). The 512 KB MV1C
   BIOSes are not supported. */
enum { S_SFIX, S_LO, S_BIOS, N_SYSTEM };

typedef struct {
    int role;
    const char *name;
    uint32_t size;
    uint32_t crc;
    const char *sha1;
    const char *label;       /* BIOS: MAME's -bios name */
} SystemFile;

static const SystemFile SYSTEM[] = {
    {S_SFIX, "sfix.sfix", 131072, 0xc2ea0cfd, "fd4a618cdcdbf849374f0a50dd8efe9dbab706c3", NULL},
    /* The PC releases' system fix (Metal Slug 3's GOG copy, mslug3_bios_sfix in MAME's format):
       28 bytes apart from MAME's (11 empty tiles with a few pixels). The Metal Slug 5 workspace's
       BIOS files come from there, for MAME and the port alike. */
    {S_SFIX, "sfix.sfix", 131072, 0x354029fc, "4ae4bf23b4c2acff875775d4cbff5583893ce2a1", NULL},
    {S_LO, "000-lo.lo", 131072, 0x5a86cff2, "5992277debadeb64d1c1c64b0a92d9293eaf7e4a", NULL},
    {S_BIOS, "sp-u2.sp1", 131072, 0xe72943de, "5c6bba07d2ec8ac95776aa3511109f5e1e2e92eb", "us"},
    {S_BIOS, "sp-e.sp1", 131072, 0x2723a5b5, "5dbff7531cf04886cde3ef022fb5ca687573dcb8", "us-e"},
    {S_BIOS, "sp1-u2", 131072, 0x62f021f4, "62d372269e1b3161c64ae21123655a0a22ffd1bb", "us-v2"},
    {S_BIOS, "sp1-u4.bin", 131072, 0x1179a30f, "866817f47aa84d903d0b819d61f6ef356893d16a", "us-u4"},
    {S_BIOS, "sp1-u3.bin", 131072, 0x2025b7a2, "73d774746196f377111cd7aa051cc8bb5dd948b3", "us-u3"},
    {S_BIOS, "sp-s2.sp1", 131072, 0x9036d879, "4f5ed7105b7128794654ce82b51723e16e389543", "euro"},
    {S_BIOS, "sp-s.sp1", 131072, 0xc7f2fa45, "09576ff20b4d6b365e78e6a5698ea450262697cd", "euro-s1"},
    {S_BIOS, "sp-s3.sp1", 131072, 0x91b64be3, "720a3e20d26818632aedf2c2fd16c54f213543e1", "asia-mv1b"},
    {S_BIOS, "vs-bios.rom", 131072, 0xf0e8f27d, "ecf01eda815909f1facec62abf3594eaa8d11075", "japan"},
    {S_BIOS, "sp-j2.sp1", 131072, 0xacede59c, "b6f97acd282fd7e94d9426078a90f059b5e9dd91", "japan-s2"},
    {S_BIOS, "sp1.jipan.1024", 131072, 0x9fb0abe4, "18a987ce2229df79a8cf6a84f968f0e42ce4e59d", "japan-s1"},
    {S_BIOS, "japan-j3.bin", 131072, 0xdff6d41f, "e92910e20092577a4523a6b39d578a71d4de7085", "japan-mv1b"},
    {S_BIOS, "sp1-j3.bin", 131072, 0xfbc6d469, "46b2b409b5b68869e367b40c846373623edb632a", "japan-j3a"},
    {S_BIOS, "sp-1v1_3db8c.bin", 131072, 0x162f0ebe, "fe1c6dd3dfcf97d960065b1bb46c1e11cb7bf271", "japan-hotel"},
    {S_BIOS, "uni-bios_4_0.rom", 131072, 0xa7aab458, "938a0bda7d9a357240718c2cec319878d36b8f72", "unibios40"},
    {S_BIOS, "uni-bios_3_3.rom", 131072, 0x24858466, "0ad92efb0c2338426635e0159d1f60b4473d0785", "unibios33"},
    {S_BIOS, "uni-bios_3_2.rom", 131072, 0xa4e8b9b3, "c92f18c3f1edda543d264ecd0ea915240e7c8258", "unibios32"},
    {S_BIOS, "uni-bios_3_1.rom", 131072, 0x0c58093f, "29329a3448c2505e1ff45ffa75e61e9693165153", "unibios31"},
    {S_BIOS, "uni-bios_3_0.rom", 131072, 0xa97c89a9, "97a5eff3b119062f10e31ad6f04fe4b90d366e7f", "unibios30"},
    {S_BIOS, "uni-bios_2_3.rom", 131072, 0x27664eb5, "5b02900a3ccf3df168bdcfc98458136fd2b92ac0", "unibios23"},
    {S_BIOS, "uni-bios_2_3o.rom", 131072, 0x601720ae, "1b8a72c720cdb5ee3f1d735bbcf447b09204b8d9", "unibios23o"},
    {S_BIOS, "uni-bios_2_2.rom", 131072, 0x2d50996a, "5241a4fb0c63b1a23fd1da8efa9c9a9bd3b4279c", "unibios22"},
    {S_BIOS, "uni-bios_2_1.rom", 131072, 0x8dabf76b, "c23732c4491d966cf0373c65c83c7a4e88f0082c", "unibios21"},
    {S_BIOS, "uni-bios_2_0.rom", 131072, 0x0c12c2ad, "37bcd4d30f3892078b46841d895a6eff16dc921e", "unibios20"},
    {S_BIOS, "uni-bios_1_3.rom", 131072, 0xb24b44a0, "eca8851d30557b97c309a0d9f4a9d20e5b14af4e", "unibios13"},
    {S_BIOS, "uni-bios_1_2.rom", 131072, 0x4fa698e9, "682e13ec1c42beaa2d04473967840c88fd52c75a", "unibios12"},
    {S_BIOS, "uni-bios_1_2o.rom", 131072, 0xe19d3ce9, "af88ef837f44a3af2d7144bb46a37c8512b67770", "unibios12o"},
    {S_BIOS, "uni-bios_1_1.rom", 131072, 0x5dda0d84, "4153d533c02926a2577e49c32657214781ff29b7", "unibios11"},
    {S_BIOS, "uni-bios_1_0.rom", 131072, 0x0ce453a0, "3b4c0cd26c176fc6b26c3a2f95143dd478f6abf9", "unibios10"},
};
#define N_SYSTEM_FILES (int)(sizeof SYSTEM / sizeof SYSTEM[0])

/* ------------------------------------------------------------------ candidates */
typedef struct {
    char path[1024];         /* loose file or zip */
    char group[1300];        /* folder of the file (zip path + inner folder for members) */
    char base[256];          /* lowercase file name */
    int in_zip;
    ZipEntry entry;
    uint32_t size;
    uint32_t crc;
    int have_crc;
} Candidate;

typedef struct {
    Candidate *c;
    int n, cap;
    int visited;
} Candidates;

#define MAX_DEPTH 4
#define MAX_VISITED 20000

static void lower_base(const char *path, char *out, size_t size)
{
    const char *b = path;
    for (const char *p = path; *p; p++) if (*p == '/' || *p == '\\') b = p + 1;
    size_t i = 0;
    for (; b[i] && i + 1 < size; i++) out[i] = (char)tolower((unsigned char)b[i]);
    out[i] = 0;
}

static int interesting(const char *base, uint32_t size)
{
    size_t n = strlen(ng_game->id);
    if (!strncmp(base, ng_game->id, n) && base[n] == '_') return 1;
    for (int k = 0; k < NG_CM_ROLES; k++)                       /* Code Mystics' generic names */
        if (ng_game->codemystics[k].name && !strcmp(base, ng_game->codemystics[k].name) &&
            size == ng_game->codemystics[k].size) return 1;
    for (int k = 0; k < ng_game->n_mame; k++)                   /* the MAME set's sizes */
        if (ng_game->mame[k].size == size) return 1;
    return size == 131072 || size == MB || size == 2 * MB || size == 4 * MB || size == 8 * MB;
}

static Candidate *add(Candidates *cs)
{
    if (cs->n == cs->cap) {
        int cap = cs->cap ? cs->cap * 2 : 64;
        Candidate *c = realloc(cs->c, (size_t)cap * sizeof *c);
        if (!c) return NULL;
        cs->c = c;
        cs->cap = cap;
    }
    Candidate *c = &cs->c[cs->n++];
    memset(c, 0, sizeof *c);
    return c;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && !strcmp(s + a - b, suffix);
}

static void add_zip(Candidates *cs, const char *path)
{
    ZipEntry *e;
    int n = zip_list(path, &e);
    for (int i = 0; i < n; i++) {
        char base[256];
        lower_base(e[i].name, base, sizeof base);
        if (!interesting(base, e[i].size)) continue;
        Candidate *c = add(cs);
        if (!c) break;
        snprintf(c->path, sizeof c->path, "%s", path);
        snprintf(c->group, sizeof c->group, "%s|%.*s", path,
                 (int)(strlen(e[i].name) - strlen(base)), e[i].name);
        snprintf(c->base, sizeof c->base, "%s", base);
        c->in_zip = 1;
        c->entry = e[i];
        c->size = e[i].size;
        c->crc = e[i].crc;
        c->have_crc = 1;
    }
    free(e);
}

static void walk(Candidates *cs, const char *path, int depth)
{
    struct stat st;
    if (cs->visited++ > MAX_VISITED || stat(path, &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        if (depth > MAX_DEPTH) return;
        DIR *d = opendir(path);
        if (!d) return;
        struct dirent *e;
        while ((e = readdir(d)) != NULL) {
            if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
            char sub[1024];
            size_t n = strlen(path);
            int slash = n && (path[n - 1] == '/' || path[n - 1] == '\\');
            snprintf(sub, sizeof sub, "%s%s%s", path, slash ? "" : "/", e->d_name);
            walk(cs, sub, depth + 1);
        }
        closedir(d);
        return;
    }
    char base[256];
    lower_base(path, base, sizeof base);
    if (ends_with(base, ".zip")) { add_zip(cs, path); return; }
    if (st.st_size > 64 * (long long)MB || !interesting(base, (uint32_t)st.st_size)) return;
    Candidate *c = add(cs);
    if (!c) return;
    snprintf(c->path, sizeof c->path, "%s", path);
    snprintf(c->group, sizeof c->group, "%.*s", (int)(strlen(path) - strlen(base)), path);
    snprintf(c->base, sizeof c->base, "%s", base);
    c->size = (uint32_t)st.st_size;
}

static uint8_t *read_candidate(const Candidate *c)
{
    if (c->in_zip) return zip_read(c->path, &c->entry);
    FILE *f = fopen(c->path, "rb");
    if (!f) return NULL;
    uint8_t *d = malloc(c->size ? c->size : 1);
    if (d && fread(d, 1, c->size, f) != c->size) { free(d); d = NULL; }
    fclose(f);
    return d;
}

static void candidate_crc(Candidate *c)
{
    if (c->have_crc) return;
    uint8_t *d = read_candidate(c);
    if (d) { c->crc = crc32_update(0, d, c->size); c->have_crc = 1; free(d); }
}

/* ------------------------------------------------------------------ helpers */
static void set_error(char *error, int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(error, (size_t)size, fmt, ap);
    va_end(ap);
}

static void swap_bytes(uint8_t *d, uint32_t n)
{
    for (uint32_t i = 0; i < n; i += 2) { uint8_t t = d[i]; d[i] = d[i + 1]; d[i + 1] = t; }
}

static int sha256_is(const uint8_t *d, uint32_t n, const char *expected)
{
    uint8_t h[32];
    char hex[65];
    sha256(d, n, h);
    hex_string(h, 32, hex);
    return !strcmp(hex, expected);
}

/* Neo Geo S format (per 32-byte tile: 4 column groups of 8 bytes, one byte per row; groups in
   pixel order 4-5, 6-7, 0-1, 2-3) to DotEmu (4 bpp rows, 4 bytes per row, low nibble left). */
static void fix_to_dotemu(const uint8_t *s, uint8_t *out, uint32_t size)
{
    static const int GROUP_OF_PAIR[4] = {2, 3, 0, 1};
    for (uint32_t t = 0; t < size; t += 32)
        for (int row = 0; row < 8; row++)
            for (int k = 0; k < 4; k++) out[t + row * 4 + k] = s[t + GROUP_OF_PAIR[k] * 8 + row];
}

/* A pair of C ROMs (odd: planes 0-1, even: planes 2-3, interleaved byte by byte) to DotEmu tiles
   (16x16, 4 bpp rows, 8 bytes per row, low nibble left). Per 128-byte Neo Geo tile: right half
   (x 8-15) at 0x00, left half at 0x40; 4 bytes per row: plane 0, plane 2, plane 1, plane 3;
   pixel x of the half is bit 7 - x. */
static void sprites_to_dotemu(const uint8_t *odd, const uint8_t *even, uint32_t size, uint8_t *out)
{
    static const int PLANE_OFFSET[4] = {0, 2, 1, 3};          /* plane -> byte in the row */
    for (uint32_t t = 0; t < size * 2 / 128; t++) {
        uint8_t region[128];
        for (int i = 0; i < 64; i++) {
            region[2 * i] = odd[t * 64 + i];
            region[2 * i + 1] = even[t * 64 + i];
        }
        uint8_t *o = out + (size_t)t * 128;
        for (int y = 0; y < 16; y++) {
            for (int x = 0; x < 16; x++) {
                const uint8_t *r = region + (x < 8 ? 0x40 : 0x00) + y * 4;
                int bit = 7 - (x & 7), px = 0;
                for (int p = 0; p < 4; p++) px |= ((r[PLANE_OFFSET[p]] >> bit) & 1) << p;
                if (x & 1) o[y * 8 + x / 2] |= (uint8_t)(px << 4);
                else o[y * 8 + x / 2] = (uint8_t)px;
            }
        }
    }
}

/* Encrypted sprites (ng_game->crypt): the C files interleaved in pairs, decrypted by the game's
   own routine, then the fix layer taken from them and the sprites converted pair by pair. */
static int crypt_sprites(const NgMameCrypt *crypt, uint8_t *const *c, const NgMameFile *const *chosen, int n,
                         uint32_t total, NgRoms *r)
{
    uint8_t *image = malloc(total), *region = malloc(total), *s = malloc(crypt->fix_size);
    uint32_t biggest = 0;
    for (int i = 0; i < n; i++) if (chosen[i]->size > biggest) biggest = chosen[i]->size;
    uint8_t *odd = malloc(biggest), *even = malloc(biggest);
    int ok = image && region && s && odd && even;
    if (ok) {
        uint32_t at = 0;
        for (int i = 0; i + 1 < n; i += 2) {
            uint32_t size = chosen[i]->size;
            for (uint32_t k = 0; k < size; k++) {
                image[at + 2 * k] = c[i][k];
                image[at + 2 * k + 1] = c[i + 1][k];
            }
            at += 2 * size;
        }
        crypt->sprites(image, total, region);
        crypt->fix(region, s);
        fix_to_dotemu(s, r->fix_game, crypt->fix_size);
        at = 0;
        for (int i = 0; i + 1 < n; i += 2) {
            uint32_t size = chosen[i]->size;
            for (uint32_t k = 0; k < size; k++) {
                odd[k] = region[at + 2 * k];
                even[k] = region[at + 2 * k + 1];
            }
            sprites_to_dotemu(odd, even, size, r->tiles + at);
            at += 2 * size;
        }
    }
    free(image); free(region); free(s); free(odd); free(even);
    return ok;
}

static void describe(NgRoms *r, int format, int verified)
{
    NgRomInfo *i = &r->info;
    char hex[65];
    i->format = format;
    i->verified = verified;
    sha256(r->p, MB, i->p1_sha256);             /* before the byte swap: file order */
    hex_string(i->p1_sha256, 32, hex);
    if (ng_game->gog_p1_sha256 && !strcmp(hex, ng_game->gog_p1_sha256)) i->version = NG_VERSION_GOG;
    sha256(r->bios, 131072, i->bios_sha256);
    if (format == NG_FORMAT_DOTEMU && !ng_has_pc_release()) i->version = NG_VERSION_ARCADE;
    snprintf(i->description, sizeof i->description, "%s, %s, BIOS %s%s",
             format == NG_FORMAT_MAME ? "MAME set" :
             format == NG_FORMAT_CODEMYSTICS ? "PC release files (Code Mystics)" :
             ng_has_pc_release() ? "PC release files (DotEmu)" : "Decrypted reference files (MAME's regions)",
             i->version == NG_VERSION_GOG ? "GOG program" :
             i->version == NG_VERSION_ARCADE ? "arcade program" : "unknown program",
             i->bios, verified ? "" : " (unverified)");
}

/* ------------------------------------------------------------------ DotEmu */
static int load_dotemu(NgRoms *r, Candidates *cs, const char *group, char *error, int size)
{
    const NgDotEmuFile *files = ng_game->dotemu;
    uint8_t *d[NG_DOTEMU_ROLES] = {0};
    int verified = 1;
    for (int k = 0; k < NG_DOTEMU_ROLES; k++) {
        const Candidate *c = NULL;
        for (int i = 0; i < cs->n && !c; i++)
            if (!strcmp(cs->c[i].group, group) && !strcmp(cs->c[i].base, files[k].name)) c = &cs->c[i];
        if (!c || c->size != files[k].size || !(d[k] = read_candidate(c))) {
            if (c) set_error(error, size, "%s does not have the expected size (%u bytes)", c->path, files[k].size);
            else set_error(error, size, "%s not found next to %s", files[k].name, files[NG_DOTEMU_P].name);
            for (int j = 0; j < NG_DOTEMU_ROLES; j++) free(d[j]);
            return -1;
        }
        if (!sha256_is(d[k], files[k].size, files[k].sha256)) verified = 0;
    }
    r->p = d[NG_DOTEMU_P]; r->p_size = files[NG_DOTEMU_P].size;
    r->bios = d[NG_DOTEMU_BIOS];
    r->fix_game = d[NG_DOTEMU_FIX_GAME]; r->fix_game_size = files[NG_DOTEMU_FIX_GAME].size;
    r->fix_bios = d[NG_DOTEMU_FIX_BIOS];
    r->tiles = d[NG_DOTEMU_TILES]; r->n_tiles = files[NG_DOTEMU_TILES].size / 128;
    r->zoom = d[NG_DOTEMU_ZOOM];
    r->z80 = d[NG_DOTEMU_Z80]; r->z80_size = files[NG_DOTEMU_Z80].size;
    r->adpcm = d[NG_DOTEMU_ADPCM]; r->adpcm_size = files[NG_DOTEMU_ADPCM].size;
    snprintf(r->info.bios, sizeof r->info.bios, "%s",
             sha256_is(r->bios, 131072, files[NG_DOTEMU_BIOS].sha256) ? "us" : "unknown");
    describe(r, NG_FORMAT_DOTEMU, verified);
    return 0;
}

/* ------------------------------------------------------------------ Code Mystics */
static uint8_t *read_mame(Candidates *cs, uint32_t size, uint32_t crc, const char *sha1_hex);

/* Code Mystics' sprites: DotEmu's tiles (16 rows of 8 bytes, 4 bpp, low nibble first) with the
   8 pixels of each half-row in reverse order (Metal Slug 4's docs/PHASE2_HARDWARE.md). The
   reversal is its own inverse. */
static void codemystics_tiles(uint8_t *t, uint32_t size)
{
    for (uint32_t i = 0; i < size; i += 4) {
        uint8_t a = t[i], b = t[i + 1], c = t[i + 2], d = t[i + 3];
        t[i] = (uint8_t)(d >> 4 | d << 4);
        t[i + 1] = (uint8_t)(c >> 4 | c << 4);
        t[i + 2] = (uint8_t)(b >> 4 | b << 4);
        t[i + 3] = (uint8_t)(a >> 4 | a << 4);
    }
}

/* The Code Mystics files of one folder (recognized by name and size, checked by SHA-256): MAME's
   regions after MAME's own transformations. The system files (BIOS, sfix.sfix, 000-lo.lo) are not
   part of the release: they come from a MAME BIOS set anywhere below the path, by hash. */
static int load_codemystics(NgRoms *r, Candidates *cs, const char *group, char *error, int size)
{
    const NgDotEmuFile *files = ng_game->codemystics;
    uint8_t *d[NG_CM_ROLES] = {0};
    uint8_t *sys[N_SYSTEM] = {0};
    const SystemFile *sys_chosen[N_SYSTEM] = {0};
    int verified = 1, result = -1;
    char missing[300] = "";
    for (int k = 0; k < NG_CM_ROLES; k++) {
        const Candidate *c = NULL;
        for (int i = 0; i < cs->n && !c; i++)
            if (!strcmp(cs->c[i].group, group) && !strcmp(cs->c[i].base, files[k].name) && cs->c[i].size == files[k].size)
                c = &cs->c[i];
        if (c && (d[k] = read_candidate(c)) != NULL) {
            if (!sha256_is(d[k], files[k].size, files[k].sha256)) verified = 0;
            continue;
        }
        size_t n = strlen(missing);
        snprintf(missing + n, sizeof missing - n, "%s%s", n ? ", " : "", files[k].name);
    }
    if (missing[0]) {
        set_error(error, size, "Incomplete GOG copy (Code Mystics): missing %s", missing);
        goto done;
    }
    for (int k = 0; k < N_SYSTEM_FILES; k++) {
        int role = SYSTEM[k].role;
        if (!sys[role] && (sys[role] = read_mame(cs, SYSTEM[k].size, SYSTEM[k].crc, SYSTEM[k].sha1)) != NULL)
            sys_chosen[role] = &SYSTEM[k];
    }
    for (int role = 0; role < N_SYSTEM; role++) {
        if (sys[role]) continue;
        const char *name = role == S_BIOS ? "an MVS BIOS" : NULL;
        for (int k = 0; k < N_SYSTEM_FILES && !name; k++) if (SYSTEM[k].role == role) name = SYSTEM[k].name;
        size_t n = strlen(missing);
        snprintf(missing + n, sizeof missing - n, "%s%s", n ? ", " : "", name);
    }
    if (missing[0]) {
        set_error(error, size, "GOG copy found, without the Neo Geo system files (%s). Copy neogeo.zip, "
                  "MAME's Neo Geo BIOS set, %s.", missing,
                  ng_roms_system_path ? "to the \"game\" folder next to the program" : "next to the game");
        goto done;
    }
    /* m1.bin is MAME's audiocpu region: the M1 at 0x10000 (128 KB), 0-0xFFFF repeating it. */
    const uint32_t z80_size = 131072;
    r->p = d[NG_CM_P]; d[NG_CM_P] = NULL; r->p_size = files[NG_CM_P].size;
    r->tiles = d[NG_CM_TILES]; d[NG_CM_TILES] = NULL; r->n_tiles = files[NG_CM_TILES].size / 128;
    codemystics_tiles(r->tiles, files[NG_CM_TILES].size);
    r->fix_game_size = files[NG_CM_FIX_GAME].size;
    r->fix_game = malloc(r->fix_game_size);
    r->fix_bios = malloc(131072);
    r->z80 = malloc(z80_size); r->z80_size = z80_size;
    r->adpcm = d[NG_CM_ADPCM]; d[NG_CM_ADPCM] = NULL; r->adpcm_size = files[NG_CM_ADPCM].size;
    r->bios = sys[S_BIOS]; sys[S_BIOS] = NULL;
    r->zoom = sys[S_LO]; sys[S_LO] = NULL;
    if (!r->fix_game || !r->fix_bios || !r->z80 || files[NG_CM_Z80].size < 0x10000 + z80_size) {
        set_error(error, size, "Out of memory");
        ng_roms_free(r);
        goto done;
    }
    fix_to_dotemu(d[NG_CM_FIX_GAME], r->fix_game, r->fix_game_size);
    fix_to_dotemu(sys[S_SFIX], r->fix_bios, 131072);
    memcpy(r->z80, d[NG_CM_Z80] + 0x10000, z80_size);
    snprintf(r->info.bios, sizeof r->info.bios, "%s", sys_chosen[S_BIOS]->label);
    describe(r, NG_FORMAT_CODEMYSTICS, verified);
    result = 0;
done:
    for (int k = 0; k < NG_CM_ROLES; k++) free(d[k]);
    for (int role = 0; role < N_SYSTEM; role++) free(sys[role]);
    return result;
}

/* ------------------------------------------------------------------ MAME */
static uint8_t *read_mame(Candidates *cs, uint32_t size, uint32_t crc, const char *sha1_hex)
{
    for (int i = 0; i < cs->n; i++) {
        Candidate *c = &cs->c[i];
        if (c->size != size) continue;
        candidate_crc(c);
        if (!c->have_crc || c->crc != crc) continue;
        uint8_t *d = read_candidate(c);
        if (!d) continue;
        uint8_t h[20];
        char hex[41];
        sha1(d, size, h);
        hex_string(h, 20, hex);
        if (!strcmp(hex, sha1_hex)) return d;
        free(d);
    }
    return NULL;
}

#define MAX_PARTS 16

/* The descriptor's entry for (kind, index): the chosen one, else the first listed. */
static const NgMameFile *mame_entry(const NgMameFile *chosen, int kind, int index)
{
    if (chosen) return chosen;
    for (int k = 0; k < ng_game->n_mame; k++)
        if (ng_game->mame[k].kind == kind && ng_game->mame[k].index == index) return &ng_game->mame[k];
    return NULL;
}

static int load_mame(NgRoms *r, Candidates *cs, char *error, int size)
{
    /* Cartridge files by kind and index; the first matching entry wins (several P1 versions). */
    uint8_t *part[NG_MAME_KINDS][MAX_PARTS] = {{0}};
    const NgMameFile *chosen[NG_MAME_KINDS][MAX_PARTS] = {{0}};
    uint32_t total[NG_MAME_KINDS] = {0};
    int count[NG_MAME_KINDS] = {0};
    uint8_t *sys[N_SYSTEM] = {0};
    const SystemFile *sys_chosen[N_SYSTEM] = {0};
    int verified = 1, result = -1;

    for (int k = 0; k < ng_game->n_mame; k++) {
        const NgMameFile *m = &ng_game->mame[k];
        if (m->index + 1 > count[m->kind]) count[m->kind] = m->index + 1;
        if (!part[m->kind][m->index] && (part[m->kind][m->index] = read_mame(cs, m->size, m->crc, m->sha1)) != NULL)
            chosen[m->kind][m->index] = m;
    }
    for (int k = 0; k < N_SYSTEM_FILES; k++) {
        int role = SYSTEM[k].role;
        if (!sys[role] && (sys[role] = read_mame(cs, SYSTEM[k].size, SYSTEM[k].crc, SYSTEM[k].sha1)) != NULL)
            sys_chosen[role] = &SYSTEM[k];
    }
    const NgMameFile *p1 = mame_entry(NULL, NG_MAME_P, 0);
    if (p1 && !part[NG_MAME_P][0]) {          /* another program version: recognized by its name */
        for (int i = 0; i < cs->n && !part[NG_MAME_P][0]; i++)
            if (!strcmp(cs->c[i].base, p1->name) && cs->c[i].size == p1->size) part[NG_MAME_P][0] = read_candidate(&cs->c[i]);
        if (part[NG_MAME_P][0]) verified = 0;
    }

    char missing[600] = "";
    int found = 0;
    for (int kind = 0; kind < NG_MAME_KINDS; kind++)
        for (int i = 0; i < count[kind]; i++) {
            const NgMameFile *m = mame_entry(chosen[kind][i], kind, i);
            if (part[kind][i]) { found++; total[kind] += m->size; continue; }
            size_t n = strlen(missing);
            snprintf(missing + n, sizeof missing - n, "%s%s", n ? ", " : "", m->name);
        }
    for (int role = 0; role < N_SYSTEM; role++) {
        if (sys[role]) { found++; continue; }
        const char *name = role == S_BIOS ? "an MVS BIOS (neogeo.zip)" : NULL;
        for (int k = 0; k < N_SYSTEM_FILES && !name; k++) if (SYSTEM[k].role == role) name = SYSTEM[k].name;
        size_t n = strlen(missing);
        snprintf(missing + n, sizeof missing - n, "%s%s", n ? ", " : "", name);
    }
    if (missing[0] || count[NG_MAME_P] == 0) {
        if (found > 2) set_error(error, size, "Incomplete MAME set: missing %s", missing);
        goto done;
    }

    const NgMameCrypt *crypt = ng_game->crypt;
    if (ng_game->mame_encrypted && !crypt) {
        set_error(error, size, "MAME set found and verified (%d files, BIOS %s), but its decryption is not "
                  "in the runtime yet", found - N_SYSTEM, sys_chosen[S_BIOS]->label);
        goto done;
    }
    r->p_size = crypt ? crypt->program_size : total[NG_MAME_P];
    r->p = malloc(r->p_size);
    r->fix_game_size = crypt ? crypt->fix_size : total[NG_MAME_S];
    r->fix_game = malloc(r->fix_game_size);
    r->fix_bios = malloc(131072);
    r->n_tiles = total[NG_MAME_C] / 128;
    r->tiles = malloc(total[NG_MAME_C]);
    r->adpcm_size = total[NG_MAME_V];
    r->adpcm = malloc(r->adpcm_size);
    r->z80_size = total[NG_MAME_M];
    r->z80 = malloc(r->z80_size);
    r->bios = sys[S_BIOS]; sys[S_BIOS] = NULL;
    r->zoom = sys[S_LO]; sys[S_LO] = NULL;
    if (!r->p || !r->fix_game || !r->fix_bios || !r->tiles || !r->adpcm || !r->z80) {
        set_error(error, size, "Out of memory");
        ng_roms_free(r);
        goto done;
    }
    uint32_t at = 0;
    uint8_t *joined = crypt ? malloc(total[NG_MAME_P]) : r->p;
    if (!joined) { set_error(error, size, "Out of memory"); ng_roms_free(r); goto done; }
    for (int i = 0; i < count[NG_MAME_P]; i++) {
        uint32_t n = mame_entry(chosen[NG_MAME_P][i], NG_MAME_P, i)->size;
        memcpy(joined + at, part[NG_MAME_P][i], n);
        at += n;
    }
    if (crypt) {
        crypt->program(joined, total[NG_MAME_P], part[NG_MAME_X][0], r->p);
        free(joined);
    }
    at = 0;
    for (int i = 0; i < count[NG_MAME_S]; i++) {
        fix_to_dotemu(part[NG_MAME_S][i], r->fix_game + at, chosen[NG_MAME_S][i]->size);
        at += chosen[NG_MAME_S][i]->size;
    }
    fix_to_dotemu(sys[S_SFIX], r->fix_bios, 131072);
    if (crypt && !crypt_sprites(crypt, part[NG_MAME_C], chosen[NG_MAME_C], count[NG_MAME_C], total[NG_MAME_C], r)) {
        set_error(error, size, "Out of memory");
        ng_roms_free(r);
        goto done;
    }
    at = 0;
    for (int i = 0; !crypt && i + 1 < count[NG_MAME_C]; i += 2) {
        uint32_t n = chosen[NG_MAME_C][i]->size;
        sprites_to_dotemu(part[NG_MAME_C][i], part[NG_MAME_C][i + 1], n, r->tiles + at);
        at += 2 * n;
    }
    at = 0;
    for (int i = 0; i < count[NG_MAME_V]; i++) {
        memcpy(r->adpcm + at, part[NG_MAME_V][i], chosen[NG_MAME_V][i]->size);
        at += chosen[NG_MAME_V][i]->size;
    }
    at = 0;
    for (int i = 0; i < count[NG_MAME_M]; i++) {
        memcpy(r->z80 + at, part[NG_MAME_M][i], chosen[NG_MAME_M][i]->size);
        at += chosen[NG_MAME_M][i]->size;
    }
    if (crypt && crypt->adpcm) crypt->adpcm(r->adpcm, r->adpcm_size);
    if (crypt && crypt->z80 && crypt->z80(r->z80, r->z80_size) != 0) {
        set_error(error, size, "The MAME set's M1 does not have the expected key");
        ng_roms_free(r);
        goto done;
    }
    if (chosen[NG_MAME_P][0] && chosen[NG_MAME_P][0]->label && !strcmp(chosen[NG_MAME_P][0]->label, "arcade"))
        r->info.version = NG_VERSION_ARCADE;
    snprintf(r->info.bios, sizeof r->info.bios, "%s", sys_chosen[S_BIOS]->label);
    describe(r, NG_FORMAT_MAME, verified);
    result = 0;
done:
    for (int kind = 0; kind < NG_MAME_KINDS; kind++)
        for (int i = 0; i < MAX_PARTS; i++) free(part[kind][i]);
    for (int role = 0; role < N_SYSTEM; role++) free(sys[role]);
    return result;
}

/* ------------------------------------------------------------------ entry point */
const char *ng_roms_system_path;

int ng_roms_load(NgRoms *r, const char *path, char *error, int error_size)
{
    memset(r, 0, sizeof *r);
    Candidates cs = {0};
    walk(&cs, path, 0);
    /* The system files may be elsewhere (a GOG install has none of them): searched there too. */
    if (ng_roms_system_path && strcmp(ng_roms_system_path, path) != 0) walk(&cs, ng_roms_system_path, 0);
    char base[256];
    lower_base(path, base, sizeof base);
    if (ends_with(base, ".zip") && strcmp(base, "neogeo.zip") != 0) {
        /* Just the game's zip (from a MAME roms folder): the BIOS is in neogeo.zip next to it. */
        char bios[1100];
        snprintf(bios, sizeof bios, "%.*sneogeo.zip", (int)(strlen(path) - strlen(base)), path);
        add_zip(&cs, bios);
    }
    int result = -1;
    set_error(error, error_size, "No %s files found in %s", ng_game->title, path);
    /* DotEmu files first (one folder with all of them), then a MAME set anywhere below. */
    for (int i = 0; i < cs.n && result != 0; i++)
        if (ng_game->dotemu[NG_DOTEMU_P].name && !strcmp(cs.c[i].base, ng_game->dotemu[NG_DOTEMU_P].name))
            result = load_dotemu(r, &cs, cs.c[i].group, error, error_size);
    /* Code Mystics files (the folder of the program file). */
    int had_codemystics = 0;
    for (int i = 0; i < cs.n && result != 0; i++)
        if (ng_game->codemystics[NG_CM_P].name && !strcmp(cs.c[i].base, ng_game->codemystics[NG_CM_P].name) &&
            cs.c[i].size == ng_game->codemystics[NG_CM_P].size) {
            result = load_codemystics(r, &cs, cs.c[i].group, error, error_size);
            had_codemystics = 1;
        }
    if (result != 0) {
        char dotemu_error[600];
        snprintf(dotemu_error, sizeof dotemu_error, "%s", error);
        if (ng_game->mame) result = load_mame(r, &cs, error, error_size);
        size_t id_len = strlen(ng_game->id);
        int had_dotemu = had_codemystics;
        for (int i = 0; i < cs.n; i++)
            had_dotemu |= !strncmp(cs.c[i].base, ng_game->id, id_len) && cs.c[i].base[id_len] == '_';
        if (result != 0 && had_dotemu) set_error(error, error_size, "%s", dotemu_error);
    }
    free(cs.c);
    if (result != 0) return -1;
    /* P and BIOS: the files keep MAME's .p byte order; the 68000 view is the swapped one. */
    swap_bytes(r->p, r->p_size);
    swap_bytes(r->bios, 131072);
    snprintf(r->info.source, sizeof r->info.source, "%s", path);
    return 0;
}

void ng_roms_free(NgRoms *r)
{
    free(r->p); free(r->bios); free(r->fix_game); free(r->fix_bios); free(r->tiles); free(r->zoom);
    free(r->z80); free(r->adpcm);
    memset(r, 0, sizeof *r);
}
