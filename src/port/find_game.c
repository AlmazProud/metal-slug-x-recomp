/* Where to look for the user's copy of the game (ng_game) when no folder was given.
 *
 * In order:
 *  1. "game" and "roms" next to the executable: the plug-and-play folders (any layout that
 *     roms.c recognizes: the PC release's folder or files, a MAME set, zips).
 *  2. The GOG installation: the GOG installer registers each game in
 *     HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\<id> with "gameName" and "path" (ng_game->gog_id,
 *     or any entry whose name contains the game's title).
 *  3. Steam libraries (Steam's registry path + steamapps\libraryfolders.vdf): any game folder
 *     under steamapps\common with resources\game\<id>_game_m68k (DotEmu) or Data\rom\p1.bin
 *     (Code Mystics, Metal Slug 4).
 *  4. The usual install folders on every drive.
 * Full recognition is done later by ng_roms_load; here it is enough that the 68000 program
 * exists. Steps 2-4 only apply to games with a PC release (ng_has_pc_release); Metal Slug 5 has
 * none, only MAME sets.
 *
 * Paths are handled in UTF-8: the executable's manifest (port.manifest) sets UTF-8 as the
 * code page, so fopen and opendir accept them.
 */
#include "neogeo.h"
#include "port.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

typedef struct {
    char (*list)[1024];
    int n, max;
} Places;

static void add(Places *p, const char *path)
{
#ifdef _WIN32
    for (int i = 0; i < p->n; i++) if (!_stricmp(p->list[i], path)) return;     /* case-insensitive paths */
#else
    for (int i = 0; i < p->n; i++) if (!strcmp(p->list[i], path)) return;
#endif
    if (p->n < p->max) snprintf(p->list[p->n++], 1024, "%s", path);
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

/* A PC release install folder: DotEmu's resources\game\<id>_game_m68k or Code Mystics'
   Data\rom\p1.bin. */
static void try_install(Places *p, const char *base)
{
    char path[1200];
    if (ng_game->dotemu[NG_DOTEMU_P].name) {
        snprintf(path, sizeof path, "%s\\resources\\game\\%s", base, ng_game->dotemu[NG_DOTEMU_P].name);
        if (exists(path)) add(p, base);
    }
    if (ng_game->codemystics[NG_CM_P].name) {
        snprintf(path, sizeof path, "%s\\Data\\rom\\%s", base, ng_game->codemystics[NG_CM_P].name);
        if (exists(path)) add(p, base);
    }
}

#ifdef _WIN32
static int registry_path(HKEY games, const wchar_t *id, char *base, int size, int require_name)
{
    wchar_t value[1024];
    DWORD n = sizeof value;
    if (require_name) {
        if (RegGetValueW(games, id, L"gameName", RRF_RT_REG_SZ, NULL, value, &n) != ERROR_SUCCESS) return 0;
        wchar_t title[128];
        if (MultiByteToWideChar(CP_UTF8, 0, ng_game->title, -1, title, 128) <= 0 || !wcsstr(value, title)) return 0;
        n = sizeof value;
    }
    if (RegGetValueW(games, id, L"path", RRF_RT_REG_SZ, NULL, value, &n) != ERROR_SUCCESS) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, value, -1, base, size, NULL, NULL) > 0;
}

static void gog(Places *p)
{
    HKEY games;
    char base[1024];
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0,
                      KEY_READ | KEY_WOW64_32KEY, &games) != ERROR_SUCCESS) return;
    wchar_t id[256];
    if (MultiByteToWideChar(CP_UTF8, 0, ng_game->gog_id, -1, id, 256) > 0 && registry_path(games, id, base, sizeof base, 0))
        try_install(p, base);
    for (DWORD i = 0;; i++) {
        DWORD n = sizeof id / sizeof id[0];
        if (RegEnumKeyExW(games, i, id, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
        if (registry_path(games, id, base, sizeof base, 1)) try_install(p, base);
    }
    RegCloseKey(games);
}

/* Every game folder of one Steam library. */
static void steam_library(Places *p, const char *library)
{
    char common[1200];
    snprintf(common, sizeof common, "%s\\steamapps\\common", library);
    DIR *d = opendir(common);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        char base[1024];
        snprintf(base, sizeof base, "%s\\%s", common, e->d_name);
        try_install(p, base);
    }
    closedir(d);
}

/* PD_STEAM_ROOT: a Steam folder instead of the registry's (tests: a simulated install). */
static void steam(Places *p)
{
    wchar_t value[1024];
    DWORD n = sizeof value;
    char root[1024];
    if (getenv("PD_STEAM_ROOT")) snprintf(root, sizeof root, "%s", getenv("PD_STEAM_ROOT"));
    else if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, NULL, value, &n) != ERROR_SUCCESS)
        return;
    else if (WideCharToMultiByte(CP_UTF8, 0, value, -1, root, sizeof root, NULL, NULL) <= 0) return;
    steam_library(p, root);
    /* Other libraries: lines  "path"  "D:\\SteamLibrary"  in libraryfolders.vdf. */
    char vdf[1200];
    snprintf(vdf, sizeof vdf, "%s\\steamapps\\libraryfolders.vdf", root);
    FILE *f = fopen(vdf, "r");
    if (!f) return;
    char line[1200];
    while (fgets(line, sizeof line, f)) {
        char *k = strstr(line, "\"path\"");
        if (!k) continue;
        char *q = strchr(k + 6, '"');
        if (!q) continue;
        char library[1024];
        int o = 0;
        for (char *c = q + 1; *c && *c != '"' && o < (int)sizeof library - 1; c++) {
            if (c[0] == '\\' && c[1] == '\\') c++;             /* "\\" escapes */
            library[o++] = *c;
        }
        library[o] = 0;
        steam_library(p, library);
    }
    fclose(f);
}

static void usual(Places *p)
{
    static const char *const FOLDERS[] = {
        "%c:\\GOG Games\\%s", "%c:\\%s",
        "%c:\\Program Files (x86)\\GOG Galaxy\\Games\\%s", "%c:\\Games\\%s",
    };
    DWORD drives = GetLogicalDrives();
    for (int u = 0; u < 26; u++) {
        if (!(drives & (1u << u))) continue;
        char letter[4] = {(char)('A' + u), ':', '\\', 0};
        UINT type = GetDriveTypeA(letter);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
        for (size_t h = 0; h < sizeof FOLDERS / sizeof FOLDERS[0]; h++) {
            char base[1024];
            snprintf(base, sizeof base, FOLDERS[h], 'A' + u, ng_game->title);
            try_install(p, base);
        }
    }
}
#endif

int game_places(const char *exe_dir, char (*list)[1024], int max)
{
    Places p = {list, 0, max};
    char path[1100];
    snprintf(path, sizeof path, "%sgame", exe_dir);
    if (exists(path)) add(&p, path);
    snprintf(path, sizeof path, "%sroms", exe_dir);
    if (exists(path)) add(&p, path);
#ifdef _WIN32
    if (ng_has_pc_release()) {
        gog(&p);
        steam(&p);
        usual(&p);
    }
#endif
    return p.n;
}
