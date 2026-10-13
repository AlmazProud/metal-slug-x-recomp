/* Is profile.h valid for the loaded copy?
 *
 * The cheats, the debug options and the red blood option use addresses found in the GOG copy's
 * program (profile.h). Another version of the program (the arcade P1 differs from GOG's, see
 * docs/ROM_MAP.md) could have its code elsewhere, so those options are only offered when the
 * code at every address they rely on is the same as in the GOG copy: one SHA-256 over those
 * windows of P1 (68000 view). The hash is of the GOG copy; the repo holds no game data.
 */
#include "profile.h"
#include "neogeo.h"
#include "hashes.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const struct { uint32_t first, last; } WINDOWS[] = {
    {0x002580, 0x0025A0},     /* debug dips cleared without the development mode (0x2592) */
    {0x0039B0, 0x003A42},     /* VBlank wait loop (PROFILE_WAIT_*) */
    {0x0040BC, 0x004100},     /* object allocation (vehicle spawn) */
    {0x015400, 0x015410},     /* development mode copied to 0x10C2BE (0x15408) */
    {0x0197DC, 0x019AF8},     /* object type table (vehicle types 0x2EC-0x318) */
    {0x01A0A6, 0x01A140},     /* stage object records (PROFILE_SPAWN_ROUTINE) */
    {0x01B2B8, 0x01B2C8},     /* BLOOD option read (0x1B2BE) */
    {0x0C07F0, 0x0C0A00},     /* weapon tables and pickup routine */
    {0x0C0BAE, 0x0C0BF4},     /* fat check (PROFILE_FAT_CHECK_*) */
    {0x0C32E4, 0x0C3320},     /* form transitions */
    {0x0C942C, 0x0C9440},     /* lives from the HERO soft dip */
};
static const char GOG_WINDOWS_SHA256[] = "4c0665fb63e1336e57dc521de523f416e8131aa249a55bcc836388a4a1c6a38f";

static void windows_hash(char hex[65])
{
    size_t total = 0;
    for (size_t i = 0; i < sizeof WINDOWS / sizeof WINDOWS[0]; i++) total += WINDOWS[i].last - WINDOWS[i].first;
    uint8_t *buffer = malloc(total), h[32];
    size_t at = 0;
    for (size_t i = 0; buffer && i < sizeof WINDOWS / sizeof WINDOWS[0]; i++) {
        memcpy(buffer + at, ng_roms.p + WINDOWS[i].first, WINDOWS[i].last - WINDOWS[i].first);
        at += WINDOWS[i].last - WINDOWS[i].first;
    }
    sha256(buffer, at, h);
    free(buffer);
    hex_string(h, 32, hex);
}

int profile_supported(void)
{
    static const uint8_t *checked;
    static int supported;
    if (!ng_roms.p) return 0;
    if (checked != ng_roms.p) {
        char hex[65];
        windows_hash(hex);
        if (getenv("PD_PROFILE_HASH")) printf("profile windows: %s\n", hex);
        supported = !strcmp(hex, GOG_WINDOWS_SHA256);
        checked = ng_roms.p;
    }
    return supported;
}
