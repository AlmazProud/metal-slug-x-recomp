/* Búsqueda de la instalación de GOG de Metal Slug X.
 *
 * El instalador de GOG registra cada juego en HKLM\SOFTWARE\WOW6432Node\GOG.com\Games\<id> con
 * los valores "gameName" y "path" (Metal Slug X: id 2046360890). Si no hay registro (copia
 * movida a mano), se prueban las carpetas habituales en cada unidad. La validación completa (SHA-256)
 * la hace después ng_roms_load; aquí basta con que exista el programa del 68000.
 *
 * Las rutas se manejan en UTF-8: el manifiesto del ejecutable (mslugx.manifest) fija UTF-8 como
 * página de códigos, así fopen las acepta.
 */
#include "port.h"

#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#define ID_GOG L"2046360890"

static int probar(const char *base, char *carpeta, int tam)
{
    char ruta[1200];
    snprintf(ruta, sizeof ruta, "%s\\resources\\game\\mslugx_game_m68k", base);
    FILE *f = fopen(ruta, "rb");
    if (!f) return 0;
    fclose(f);
    snprintf(carpeta, (size_t)tam, "%s\\resources\\game", base);
    return 1;
}

#ifdef _WIN32
static int ruta_del_registro(HKEY juegos, const wchar_t *id, char *base, int tam, int exigir_nombre)
{
    wchar_t value[1024];
    DWORD n = sizeof value;
    if (exigir_nombre) {
        if (RegGetValueW(juegos, id, L"gameName", RRF_RT_REG_SZ, NULL, value, &n) != ERROR_SUCCESS) return 0;
        if (!wcsstr(value, L"Metal Slug X")) return 0;
        n = sizeof value;
    }
    if (RegGetValueW(juegos, id, L"path", RRF_RT_REG_SZ, NULL, value, &n) != ERROR_SUCCESS) return 0;
    return WideCharToMultiByte(CP_UTF8, 0, value, -1, base, tam, NULL, NULL) > 0;
}
#endif

int gog_find(char *carpeta, int tam)
{
    char base[1024];
#ifdef _WIN32
    HKEY juegos;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\GOG.com\\Games", 0,
                      KEY_READ | KEY_WOW64_32KEY, &juegos) == ERROR_SUCCESS) {
        int ok = ruta_del_registro(juegos, ID_GOG, base, sizeof base, 0) && probar(base, carpeta, tam);
        wchar_t id[256];
        for (DWORD i = 0; !ok; i++) {
            DWORD n = sizeof id / sizeof id[0];
            if (RegEnumKeyExW(juegos, i, id, &n, NULL, NULL, NULL, NULL) != ERROR_SUCCESS) break;
            ok = ruta_del_registro(juegos, id, base, sizeof base, 1) && probar(base, carpeta, tam);
        }
        RegCloseKey(juegos);
        if (ok) return 1;
    }
    DWORD unidades = GetLogicalDrives();
    static const char *const habituales[] = {
        "%c:\\GOG Games\\Metal Slug X", "%c:\\Metal Slug X",
        "%c:\\Program Files (x86)\\GOG Galaxy\\Games\\Metal Slug X", "%c:\\Games\\Metal Slug X",
    };
    for (int u = 0; u < 26; u++) {
        if (!(unidades & (1u << u))) continue;
        char letra[4] = {(char)('A' + u), ':', '\\', 0};
        UINT tipo = GetDriveTypeA(letra);
        if (tipo != DRIVE_FIXED && tipo != DRIVE_REMOVABLE) continue;
        for (size_t h = 0; h < sizeof habituales / sizeof habituales[0]; h++) {
            snprintf(base, sizeof base, habituales[h], 'A' + u);
            if (probar(base, carpeta, tam)) return 1;
        }
    }
#else
    (void)base; (void)carpeta; (void)tam;
#endif
    return 0;
}
