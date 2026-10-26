/* Metal Slug X (SNK, 1999): game descriptor (hashes only, never data).
 *
 * Built into mslugx.exe, the regression target: the runtime made generic for Metal Slug 3 must
 * keep passing the Metal Slug X tests against MAME. Hashes from `mame -listxml mslugx` (MAME
 * 0.289) and from the GOG copy (Metal Slug X port, docs/ROM_MAP.md there).
 */
#include "neogeo.h"

static const NgMameFile MAME[] = {
    {NG_MAME_P, 0, "250-p1.p1", 1048576, 0x81f1f60b, "4c19f2e9824e606178ac1c9d4b0516fbaa625035", "arcade"},
    {NG_MAME_P, 0, "250-p1.p1", 1048576, 0x60c33b1a, "8999bfc42b233cdb58778857c87d9f3187c61025", "gog"},
    {NG_MAME_P, 1, "250-p2.ep1", 4194304, 0x1fda2e12, "18aaa7a3ba8da99f78c430e9be69ccde04bc04d9", NULL},
    {NG_MAME_S, 0, "250-s1.s1", 131072, 0xfb6f441d, "2cc392ecde5d5afb28ddbaa1030552b48571dcfb", NULL},
    {NG_MAME_M, 0, "250-m1.m1", 131072, 0xfd42a842, "55769bad4860f64ef53a333e0da9e073db483d6a", NULL},
    {NG_MAME_V, 0, "250-v1.v1", 4194304, 0xc79ede73, "ebfcc67204ff9677cf7972fd5b6b7faabf07280c", NULL},
    {NG_MAME_V, 1, "250-v2.v2", 4194304, 0xea9aabe1, "526c42ca9a388f7435569400e2f132e2724c71ff", NULL},
    {NG_MAME_V, 2, "250-v3.v3", 2097152, 0x2ca65102, "45979d1edb1fc774a415d9386f98d7cb252a2043", NULL},
    {NG_MAME_C, 0, "250-c1.c1", 8388608, 0x09a52c6f, "c3e8a8ccdac0f8bddc4c3413277626532405fae2", NULL},
    {NG_MAME_C, 1, "250-c2.c2", 8388608, 0x31679821, "554f600a3aa09c16c13c625299b087a79d0d15c5", NULL},
    {NG_MAME_C, 2, "250-c3.c3", 8388608, 0xfd602019, "c56646c62387bc1439d46610258c755beb8d7dd8", NULL},
    {NG_MAME_C, 3, "250-c4.c4", 8388608, 0x31354513, "31be8ea2498001f68ce4b06b8b90acbf2dcab6af", NULL},
    {NG_MAME_C, 4, "250-c5.c5", 8388608, 0xa4b56124, "d41069856df990a1a99d39fb263c8303389d5475", NULL},
    {NG_MAME_C, 5, "250-c6.c6", 8388608, 0x83e3e69d, "39be66287696829d243fb71b3fb8b7dc2bc3298f", NULL},
};

static const NgGame GAME = {
    .id = "mslugx",
    .title = "Metal Slug X",
    .project = "metal-slug-x-recomp",
    .gog_id = "2046360890",
    .dotemu = {
        [NG_DOTEMU_P] = {"mslugx_game_m68k", 5242880, "5313f40dd217140b27f033f95b90f8119f4ed4e6401533d699f1df874b14304b"},
        [NG_DOTEMU_BIOS] = {"mslugx_bios_m68k", 131072, "01ff1db85c5b209d4f5879ea8348e3b39a237532c37232f14c2b48945e9aa7d8"},
        [NG_DOTEMU_FIX_GAME] = {"mslugx_game_sfix", 131072, "d8acb320a8d40424f67fff7e6181970772294a2f58ce8bc551cefb0427256b06"},
        [NG_DOTEMU_FIX_BIOS] = {"mslugx_bios_sfix", 131072, "5e8dcbdfdb29b8a273a2bf40582946a84067975e98955d2b7b91c39f82a1fa0e"},
        [NG_DOTEMU_TILES] = {"mslugx_tiles", 50331648, "0945f34d952a4dfed664122440d032059eb49bf2a30a09351a395d77e5cfcd83"},
        [NG_DOTEMU_ZOOM] = {"mslugx_zoom_table", 131072, "2a76f023dc578804c6bcd58ed514ffc04fcb433b3bd75d4452575ffda57a0707"},
        [NG_DOTEMU_Z80] = {"mslugx_game_z80", 131072, "6a132323fa9f41ae0712644cc804e113257baf04b270c63f93dbd5193f02ffad"},
        [NG_DOTEMU_ADPCM] = {"mslugx_adpcm", 10485760, "b45ca27aa08aecec6f76bdc60e6066e72d80c6a37df18d3d62b6e06d6d1dc40c"},
    },
    .gog_p1_sha256 = "4cd83f69512d2ad8c1cc60e23b3eebf38cdd534fa9866a8723164349ca429404",
    .mame = MAME,
    .n_mame = (int)(sizeof MAME / sizeof MAME[0]),
    .cart = NG_CART_ALTERA_MSLUGX,
};

const NgGame *const ng_game = &GAME;
