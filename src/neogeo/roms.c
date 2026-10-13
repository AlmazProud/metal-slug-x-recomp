/* Loading of the user's copy ("bring your own ROM").
 *
 * `path` can be a folder (searched up to a few levels deep) or a zip. Two layouts are
 * recognized:
 *
 *  - DotEmu files (the PC releases: GOG, and most likely Steam): resources\game\mslugx_*.
 *    Validated by SHA-256 against the GOG copy (docs/ROM_MAP.md). Files of the right size with
 *    another hash still load, marked as an unverified version.
 *  - A MAME set (an arcade board dump): mslugx + neogeo, as zips or loose files. Each file is
 *    recognized by its CRC-32 and confirmed by SHA-1, the hashes MAME publishes (mame -listxml
 *    mslugx), whatever its name; the graphics are converted to the DotEmu layout the runtime
 *    uses (the inverse of tools/extract_roms.py).
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
/* SHA-256 of the GOG copy's P1 (first MB of the 68000 program, file byte order). */
static const char GOG_P1_SHA256[] = "4cd83f69512d2ad8c1cc60e23b3eebf38cdd534fa9866a8723164349ca429404";

typedef struct {
    const char *name;
    uint32_t size;
    const char *sha256;
} DotEmuFile;

static const DotEmuFile DOTEMU[] = {
    {"mslugx_game_m68k", 5242880, "5313f40dd217140b27f033f95b90f8119f4ed4e6401533d699f1df874b14304b"},
    {"mslugx_bios_m68k", 131072, "01ff1db85c5b209d4f5879ea8348e3b39a237532c37232f14c2b48945e9aa7d8"},
    {"mslugx_game_sfix", 131072, "d8acb320a8d40424f67fff7e6181970772294a2f58ce8bc551cefb0427256b06"},
    {"mslugx_bios_sfix", 131072, "5e8dcbdfdb29b8a273a2bf40582946a84067975e98955d2b7b91c39f82a1fa0e"},
    {"mslugx_tiles", 50331648, "0945f34d952a4dfed664122440d032059eb49bf2a30a09351a395d77e5cfcd83"},
    {"mslugx_zoom_table", 131072, "2a76f023dc578804c6bcd58ed514ffc04fcb433b3bd75d4452575ffda57a0707"},
    {"mslugx_game_z80", 131072, "6a132323fa9f41ae0712644cc804e113257baf04b270c63f93dbd5193f02ffad"},
    {"mslugx_adpcm", 10485760, "b45ca27aa08aecec6f76bdc60e6066e72d80c6a37df18d3d62b6e06d6d1dc40c"},
};
#define N_DOTEMU (int)(sizeof DOTEMU / sizeof DOTEMU[0])

enum { R_P1, R_P2, R_S1, R_M1, R_V1, R_V2, R_V3, R_C1, R_C2, R_C3, R_C4, R_C5, R_C6, R_SFIX, R_LO,
       R_BIOS };
#define N_ROLES (R_BIOS + 1)

typedef struct {
    int role;
    const char *name;        /* MAME's name (for messages; files are matched by hash) */
    uint32_t size;
    uint32_t crc;
    const char *sha1;
    const char *label;       /* P1: the version; BIOS: MAME's -bios name */
} MameFile;

/* From `mame -listxml mslugx` (MAME 0.289), plus the GOG copy's P1. BIOSes in order of
   preference: the US MVS BIOS first (it is the GOG copy's BIOS, the one the port was verified
   with). The 512 KB MV1C BIOSes are not supported. */
static const MameFile MAME[] = {
    {R_P1, "250-p1.p1", 1048576, 0x81f1f60b, "4c19f2e9824e606178ac1c9d4b0516fbaa625035", "arcade"},
    {R_P1, "250-p1.p1", 1048576, 0x60c33b1a, "8999bfc42b233cdb58778857c87d9f3187c61025", "gog"},
    {R_P2, "250-p2.ep1", 4194304, 0x1fda2e12, "18aaa7a3ba8da99f78c430e9be69ccde04bc04d9", NULL},
    {R_S1, "250-s1.s1", 131072, 0xfb6f441d, "2cc392ecde5d5afb28ddbaa1030552b48571dcfb", NULL},
    {R_M1, "250-m1.m1", 131072, 0xfd42a842, "55769bad4860f64ef53a333e0da9e073db483d6a", NULL},
    {R_V1, "250-v1.v1", 4194304, 0xc79ede73, "ebfcc67204ff9677cf7972fd5b6b7faabf07280c", NULL},
    {R_V2, "250-v2.v2", 4194304, 0xea9aabe1, "526c42ca9a388f7435569400e2f132e2724c71ff", NULL},
    {R_V3, "250-v3.v3", 2097152, 0x2ca65102, "45979d1edb1fc774a415d9386f98d7cb252a2043", NULL},
    {R_C1, "250-c1.c1", 8388608, 0x09a52c6f, "c3e8a8ccdac0f8bddc4c3413277626532405fae2", NULL},
    {R_C2, "250-c2.c2", 8388608, 0x31679821, "554f600a3aa09c16c13c625299b087a79d0d15c5", NULL},
    {R_C3, "250-c3.c3", 8388608, 0xfd602019, "c56646c62387bc1439d46610258c755beb8d7dd8", NULL},
    {R_C4, "250-c4.c4", 8388608, 0x31354513, "31be8ea2498001f68ce4b06b8b90acbf2dcab6af", NULL},
    {R_C5, "250-c5.c5", 8388608, 0xa4b56124, "d41069856df990a1a99d39fb263c8303389d5475", NULL},
    {R_C6, "250-c6.c6", 8388608, 0x83e3e69d, "39be66287696829d243fb71b3fb8b7dc2bc3298f", NULL},
    {R_SFIX, "sfix.sfix", 131072, 0xc2ea0cfd, "fd4a618cdcdbf849374f0a50dd8efe9dbab706c3", NULL},
    {R_LO, "000-lo.lo", 131072, 0x5a86cff2, "5992277debadeb64d1c1c64b0a92d9293eaf7e4a", NULL},
    {R_BIOS, "sp-u2.sp1", 131072, 0xe72943de, "5c6bba07d2ec8ac95776aa3511109f5e1e2e92eb", "us"},
    {R_BIOS, "sp-e.sp1", 131072, 0x2723a5b5, "5dbff7531cf04886cde3ef022fb5ca687573dcb8", "us-e"},
    {R_BIOS, "sp1-u2", 131072, 0x62f021f4, "62d372269e1b3161c64ae21123655a0a22ffd1bb", "us-v2"},
    {R_BIOS, "sp1-u4.bin", 131072, 0x1179a30f, "866817f47aa84d903d0b819d61f6ef356893d16a", "us-u4"},
    {R_BIOS, "sp1-u3.bin", 131072, 0x2025b7a2, "73d774746196f377111cd7aa051cc8bb5dd948b3", "us-u3"},
    {R_BIOS, "sp-s2.sp1", 131072, 0x9036d879, "4f5ed7105b7128794654ce82b51723e16e389543", "euro"},
    {R_BIOS, "sp-s.sp1", 131072, 0xc7f2fa45, "09576ff20b4d6b365e78e6a5698ea450262697cd", "euro-s1"},
    {R_BIOS, "sp-s3.sp1", 131072, 0x91b64be3, "720a3e20d26818632aedf2c2fd16c54f213543e1", "asia-mv1b"},
    {R_BIOS, "vs-bios.rom", 131072, 0xf0e8f27d, "ecf01eda815909f1facec62abf3594eaa8d11075", "japan"},
    {R_BIOS, "sp-j2.sp1", 131072, 0xacede59c, "b6f97acd282fd7e94d9426078a90f059b5e9dd91", "japan-s2"},
    {R_BIOS, "sp1.jipan.1024", 131072, 0x9fb0abe4, "18a987ce2229df79a8cf6a84f968f0e42ce4e59d", "japan-s1"},
    {R_BIOS, "japan-j3.bin", 131072, 0xdff6d41f, "e92910e20092577a4523a6b39d578a71d4de7085", "japan-mv1b"},
    {R_BIOS, "sp1-j3.bin", 131072, 0xfbc6d469, "46b2b409b5b68869e367b40c846373623edb632a", "japan-j3a"},
    {R_BIOS, "sp-1v1_3db8c.bin", 131072, 0x162f0ebe, "fe1c6dd3dfcf97d960065b1bb46c1e11cb7bf271", "japan-hotel"},
    {R_BIOS, "uni-bios_4_0.rom", 131072, 0xa7aab458, "938a0bda7d9a357240718c2cec319878d36b8f72", "unibios40"},
    {R_BIOS, "uni-bios_3_3.rom", 131072, 0x24858466, "0ad92efb0c2338426635e0159d1f60b4473d0785", "unibios33"},
    {R_BIOS, "uni-bios_3_2.rom", 131072, 0xa4e8b9b3, "c92f18c3f1edda543d264ecd0ea915240e7c8258", "unibios32"},
    {R_BIOS, "uni-bios_3_1.rom", 131072, 0x0c58093f, "29329a3448c2505e1ff45ffa75e61e9693165153", "unibios31"},
    {R_BIOS, "uni-bios_3_0.rom", 131072, 0xa97c89a9, "97a5eff3b119062f10e31ad6f04fe4b90d366e7f", "unibios30"},
    {R_BIOS, "uni-bios_2_3.rom", 131072, 0x27664eb5, "5b02900a3ccf3df168bdcfc98458136fd2b92ac0", "unibios23"},
    {R_BIOS, "uni-bios_2_3o.rom", 131072, 0x601720ae, "1b8a72c720cdb5ee3f1d735bbcf447b09204b8d9", "unibios23o"},
    {R_BIOS, "uni-bios_2_2.rom", 131072, 0x2d50996a, "5241a4fb0c63b1a23fd1da8efa9c9a9bd3b4279c", "unibios22"},
    {R_BIOS, "uni-bios_2_1.rom", 131072, 0x8dabf76b, "c23732c4491d966cf0373c65c83c7a4e88f0082c", "unibios21"},
    {R_BIOS, "uni-bios_2_0.rom", 131072, 0x0c12c2ad, "37bcd4d30f3892078b46841d895a6eff16dc921e", "unibios20"},
    {R_BIOS, "uni-bios_1_3.rom", 131072, 0xb24b44a0, "eca8851d30557b97c309a0d9f4a9d20e5b14af4e", "unibios13"},
    {R_BIOS, "uni-bios_1_2.rom", 131072, 0x4fa698e9, "682e13ec1c42beaa2d04473967840c88fd52c75a", "unibios12"},
    {R_BIOS, "uni-bios_1_2o.rom", 131072, 0xe19d3ce9, "af88ef837f44a3af2d7144bb46a37c8512b67770", "unibios12o"},
    {R_BIOS, "uni-bios_1_1.rom", 131072, 0x5dda0d84, "4153d533c02926a2577e49c32657214781ff29b7", "unibios11"},
    {R_BIOS, "uni-bios_1_0.rom", 131072, 0x0ce453a0, "3b4c0cd26c176fc6b26c3a2f95143dd478f6abf9", "unibios10"},
};
#define N_MAME (int)(sizeof MAME / sizeof MAME[0])

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
    if (!strncmp(base, "mslugx_", 7)) return 1;
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

static void describe(NgRoms *r, int format, int verified)
{
    NgRomInfo *i = &r->info;
    char hex[65];
    i->format = format;
    i->verified = verified;
    sha256(r->p, MB, i->p1_sha256);             /* before the byte swap: file order */
    hex_string(i->p1_sha256, 32, hex);
    if (!strcmp(hex, GOG_P1_SHA256)) i->version = NG_VERSION_GOG;
    sha256(r->bios, 131072, i->bios_sha256);
    snprintf(i->description, sizeof i->description, "%s, %s, BIOS %s%s",
             format == NG_FORMAT_DOTEMU ? "PC release files (DotEmu)" : "MAME set",
             i->version == NG_VERSION_GOG ? "GOG program" :
             i->version == NG_VERSION_ARCADE ? "arcade program" : "unknown program",
             i->bios, verified ? "" : " (unverified)");
}

/* ------------------------------------------------------------------ DotEmu */
static int load_dotemu(NgRoms *r, Candidates *cs, const char *group, char *error, int size)
{
    uint8_t *d[N_DOTEMU] = {0};
    int verified = 1;
    for (int k = 0; k < N_DOTEMU; k++) {
        const Candidate *c = NULL;
        for (int i = 0; i < cs->n && !c; i++)
            if (!strcmp(cs->c[i].group, group) && !strcmp(cs->c[i].base, DOTEMU[k].name)) c = &cs->c[i];
        if (!c || c->size != DOTEMU[k].size || !(d[k] = read_candidate(c))) {
            if (c) set_error(error, size, "%s does not have the expected size (%u bytes)", c->path, DOTEMU[k].size);
            else set_error(error, size, "%s not found next to mslugx_game_m68k", DOTEMU[k].name);
            for (int j = 0; j < N_DOTEMU; j++) free(d[j]);
            return -1;
        }
        if (!sha256_is(d[k], DOTEMU[k].size, DOTEMU[k].sha256)) verified = 0;
    }
    r->p = d[0]; r->p_size = DOTEMU[0].size;
    r->bios = d[1];
    r->fix_game = d[2];
    r->fix_bios = d[3];
    r->tiles = d[4]; r->n_tiles = DOTEMU[4].size / 128;
    r->zoom = d[5];
    r->z80 = d[6];
    r->adpcm = d[7]; r->adpcm_size = DOTEMU[7].size;
    snprintf(r->info.bios, sizeof r->info.bios, "%s", sha256_is(r->bios, 131072, DOTEMU[1].sha256) ? "us" : "unknown");
    describe(r, NG_FORMAT_DOTEMU, verified);
    return 0;
}

/* ------------------------------------------------------------------ MAME */
static uint8_t *read_mame(Candidates *cs, const MameFile *m)
{
    for (int i = 0; i < cs->n; i++) {
        Candidate *c = &cs->c[i];
        if (c->size != m->size) continue;
        candidate_crc(c);
        if (!c->have_crc || c->crc != m->crc) continue;
        uint8_t *d = read_candidate(c);
        if (!d) continue;
        uint8_t h[20];
        char hex[41];
        sha1(d, m->size, h);
        hex_string(h, 20, hex);
        if (!strcmp(hex, m->sha1)) return d;
        free(d);
    }
    return NULL;
}

static int load_mame(NgRoms *r, Candidates *cs, char *error, int size)
{
    uint8_t *d[N_ROLES] = {0};
    const MameFile *chosen[N_ROLES] = {0};
    int verified = 1;
    for (int k = 0; k < N_MAME; k++) {
        int role = MAME[k].role;
        if (!d[role] && (d[role] = read_mame(cs, &MAME[k])) != NULL) chosen[role] = &MAME[k];
    }
    if (!d[R_P1]) {                     /* another program version: recognized by its name */
        for (int i = 0; i < cs->n && !d[R_P1]; i++)
            if (!strcmp(cs->c[i].base, "250-p1.p1") && cs->c[i].size == MB) d[R_P1] = read_candidate(&cs->c[i]);
        if (d[R_P1]) verified = 0;
    }
    char missing[600] = "";
    int found = 0;
    for (int role = 0; role < N_ROLES; role++) {
        if (d[role]) { found++; continue; }
        const char *name = role == R_BIOS ? "an MVS BIOS (neogeo.zip)" : NULL;
        for (int k = 0; k < N_MAME && !name; k++) if (MAME[k].role == role) name = MAME[k].name;
        size_t n = strlen(missing);
        snprintf(missing + n, sizeof missing - n, "%s%s", n ? ", " : "", name);
    }
    if (missing[0]) {
        if (found > 2) set_error(error, size, "Incomplete MAME set: missing %s", missing);
        for (int role = 0; role < N_ROLES; role++) free(d[role]);
        return -1;
    }
    r->p_size = 5 * MB;
    r->p = malloc(r->p_size);
    r->fix_game = malloc(131072);
    r->fix_bios = malloc(131072);
    r->n_tiles = 48 * MB / 128;
    r->tiles = malloc(48 * MB);
    r->adpcm_size = 10 * MB;
    r->adpcm = malloc(r->adpcm_size);
    r->bios = d[R_BIOS];
    r->zoom = d[R_LO];
    r->z80 = d[R_M1];
    if (!r->p || !r->fix_game || !r->fix_bios || !r->tiles || !r->adpcm) {
        set_error(error, size, "Out of memory");
        for (int role = 0; role < N_ROLES; role++)
            if (role != R_BIOS && role != R_LO && role != R_M1) free(d[role]);
        ng_roms_free(r);
        return -1;
    }
    memcpy(r->p, d[R_P1], MB);
    memcpy(r->p + MB, d[R_P2], 4 * MB);
    fix_to_dotemu(d[R_S1], r->fix_game, 131072);
    fix_to_dotemu(d[R_SFIX], r->fix_bios, 131072);
    for (int k = 0; k < 3; k++)
        sprites_to_dotemu(d[R_C1 + 2 * k], d[R_C2 + 2 * k], 8 * MB, r->tiles + (size_t)k * 16 * MB);
    memcpy(r->adpcm, d[R_V1], 4 * MB);
    memcpy(r->adpcm + 4 * MB, d[R_V2], 4 * MB);
    memcpy(r->adpcm + 8 * MB, d[R_V3], 2 * MB);
    for (int role = 0; role < N_ROLES; role++)
        if (role != R_BIOS && role != R_LO && role != R_M1) free(d[role]);
    if (chosen[R_P1] && !strcmp(chosen[R_P1]->label, "arcade")) r->info.version = NG_VERSION_ARCADE;
    snprintf(r->info.bios, sizeof r->info.bios, "%s", chosen[R_BIOS]->label);
    describe(r, NG_FORMAT_MAME, verified);
    return 0;
}

/* ------------------------------------------------------------------ entry point */
int ng_roms_load(NgRoms *r, const char *path, char *error, int error_size)
{
    memset(r, 0, sizeof *r);
    Candidates cs = {0};
    walk(&cs, path, 0);
    char base[256];
    lower_base(path, base, sizeof base);
    if (ends_with(base, ".zip") && strcmp(base, "neogeo.zip") != 0) {
        /* Just the game's zip (from a MAME roms folder): the BIOS is in neogeo.zip next to it. */
        char bios[1100];
        snprintf(bios, sizeof bios, "%.*sneogeo.zip", (int)(strlen(path) - strlen(base)), path);
        add_zip(&cs, bios);
    }
    int result = -1;
    set_error(error, error_size, "No Metal Slug X files found in %s", path);
    /* DotEmu files first (one folder with all of them), then a MAME set anywhere below. */
    for (int i = 0; i < cs.n && result != 0; i++)
        if (!strcmp(cs.c[i].base, "mslugx_game_m68k")) result = load_dotemu(r, &cs, cs.c[i].group, error, error_size);
    if (result != 0) {
        char dotemu_error[600];
        snprintf(dotemu_error, sizeof dotemu_error, "%s", error);
        result = load_mame(r, &cs, error, error_size);
        int had_dotemu = 0;
        for (int i = 0; i < cs.n; i++) had_dotemu |= !strncmp(cs.c[i].base, "mslugx_", 7);
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
