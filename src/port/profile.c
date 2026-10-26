/* Is the game profile valid for the loaded copy?
 *
 * The cheats, the debug options and the red blood option use addresses found in the reference
 * copy's program (game_profile). Another version of the program could have its code elsewhere
 * (Metal Slug X: the arcade P1 differs from GOG's), so those options are only offered when the
 * code at every address they rely on is the same as in the reference copy: one SHA-256 over
 * those windows of the program (68000 view). The repo holds the hash only, no game data.
 */
#include "profile.h"
#include "neogeo.h"
#include "hashes.h"
#include "sha256.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static void windows_hash(char hex[65])
{
    size_t total = 0;
    const ProfileWindow *w = game_profile.windows;
    for (int i = 0; i < game_profile.n_windows; i++) total += w[i].last - w[i].first;
    uint8_t *buffer = malloc(total), h[32];
    size_t at = 0;
    for (int i = 0; buffer && i < game_profile.n_windows; i++) {
        memcpy(buffer + at, ng_roms.p + w[i].first, w[i].last - w[i].first);
        at += w[i].last - w[i].first;
    }
    sha256(buffer, at, h);
    free(buffer);
    hex_string(h, 32, hex);
}

int profile_supported(void)
{
    static const uint8_t *checked;
    static int supported;
    if (!ng_roms.p || game_profile.n_windows == 0) return 0;
    if (checked != ng_roms.p) {
        char hex[65];
        windows_hash(hex);
        if (getenv("PD_PROFILE_HASH")) printf("profile windows: %s\n", hex);
        supported = !strcmp(hex, game_profile.windows_sha256);
        checked = ng_roms.p;
    }
    return supported;
}
