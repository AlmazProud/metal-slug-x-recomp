/* Locating the GOG installation of Metal Slug X.
 *
 * The GOG installer registers each game in HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\<id> with
 * the values "gameName" and "path" (Metal Slug X: id 2046360890). If there is no registry entry
 * (copy moved by hand), the usual folders on every drive are tried. Full validation (SHA-256)
 * is done later by ng_roms_load; here it is enough that the 68000 program exists.
 *
 * Paths are handled in UTF-8: the executable's manifest (mslugx.manifest) sets UTF-8 as the
 * code page, so fopen accepts them.
 */
#include "port.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define GOG_ID L"2046360890"

static int try_folder(const char *base, char *folder, int size)
{
    char path[1200];
    snprintf(path, sizeof path, "%s\\resources\\game\\mslugx_game_m68k", base);
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    snprintf(folder, (size_t)size, "%s\\resources\\game", base);
    return 1;
}

#ifdef _WIN32
static int registry_path(HKEY games, const wchar_t *id, char *base, int size, int require_name)
{
    wchar_t value[1024];
    DWORD n = sizeof value;
    if (require_name) {
        if (RegGetValueW(games, id, L"gameName", RRF_RT_REG_SZ, NULL, value, &n) != ERROR_SUCCESS) return 0;
        if (!wcsstr(value, L"Metal Slug X")) return 0;
        n = sizeof value;
    }
    if (RegGetValueW(games, id, L"path", RRF_RT_REG_SZ, NULL, value, &n) != ERROR_SUCCESS) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, value, -1, base, size, NULL, NULL) > 0;
}
#endif

int gog_find(char *folder, int size)
{
    char base[1024];
#ifdef _WIN32
    HKEY games;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0,
                      KEY_READ | KEY_WOW64_32KEY, &games) == ERROR_SUCCESS) {
        int ok = registry_path(games, GOG_ID, base, sizeof base, 0) && try_folder(base, folder, size);
        wchar_t id[256];
        for (DWORD i = 0; !ok; i++) {
            DWORD n = sizeof id / sizeof id[0];
            if (RegEnumKeyExW(games, i, id, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            ok = registry_path(games, id, base, sizeof base, 1) && try_folder(base, folder, size);
        }
        RegCloseKey(games);
        if (ok) return 1;
    }
    DWORD drives = GetLogicalDrives();
    static const char *const usual[] = {
        "%c:\\GOG Games\\Metal Slug X", "%c:\\Metal Slug X",
        "%c:\\Program Files (x86)\\GOG Galaxy\\Games\\Metal Slug X", "%c:\\Games\\Metal Slug X",
    };
    for (int u = 0; u < 26; u++) {
        if (!(drives & (1u << u))) continue;
        char letter[4] = {(char)('A' + u), ':', '\\', 0};
        UINT type = GetDriveTypeA(letter);
        if (type != DRIVE_FIXED && type != DRIVE_REMOVABLE) continue;
        for (size_t h = 0; h < sizeof usual / sizeof usual[0]; h++) {
            snprintf(base, sizeof base, usual[h], 'A' + u);
            if (try_folder(base, folder, size)) return 1;
        }
    }
#else
    (void)base; (void)folder; (void)size;
#endif
    return 0;
}
