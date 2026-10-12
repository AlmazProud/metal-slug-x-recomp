/* Metal Slug X specific data used by the port, found during the analysis (P1 listing) and
 * confirmed with the recorded Mission 1 playthrough. A first step towards a "game profile".
 */
#ifndef PROFILE_H
#define PROFILE_H

/* Main loop: waits for VBlank at 0x39B0-0x3A3E (bclr #7,-$7F75(a5); beq) and exits at 0x3A42. */
#define PROFILE_WAIT_START 0x39B0u
#define PROFILE_WAIT_END 0x3A42u

/* On leaving the wait, the game stores at -$7F76(a5) = 0x10008A how many VBlanks the logic cycle
 * took: normally 2 (30 Hz), 3 or 4 with slowdown (20 or 15 Hz). Offset in RAM. */
#define PROFILE_RAM_FRAMES_PER_TICK 0x008Au

/* Blood: the game options in the BIOS service menu (soft dips, table in the P ROM header)
 * include "BLOOD OFF/ON"; in the US and Europe it defaults to OFF (white blood). The BIOS
 * copies the options to 0x10FD84: two times, two counts and ten options from 0x10FD8A
 * (CONTINUE, DIFFICULTY, PLAY TIME, DEMO SOUND, PLAY MANUAL, BLOOD...). The game checks it at
 * 0x1B2BE (tst.b $10FD8F). Writing that byte from power-on breaks the BIOS RAM test: only the
 * game code's read is forced (ng_forced_read, low byte of the word). */
/* Game debug dips: two bytes that only take effect with the BIOS development mode
 * (0x10FE80); otherwise the game clears them every frame (0x2592). Forced like the blood. */
#define PROFILE_DIPS_WORD 0x10F000
/* BIOS development mode (high byte of the word): opens the developers' hidden menu. */
#define PROFILE_DEVMODE_WORD 0x10FE80
/* The game copies the development mode flag to 0x10C2BE when a game starts (0x15408) and the
 * pause code checks the copy: forced too (high byte), so the mode works mid-mission. */
#define PROFILE_DEVMODE_COPY_WORD 0x10C2BE

/* Game state (0x100088): 2 while a mission is being played. */
#define PROFILE_GAME_STATE 0x100088u
#define PROFILE_GAME_STATE_PLAYING 2
/* Per-player table at 0x10C718 (bytes interleaved P1, P2; words at +0xC P1, +0xE P2):
 * +0 0 on foot, 2 in the Slug (0xC07F8), +2 shot counter, +4 weapon sub-type, +6 weapon, +8 bombs,
 * +0xA bomb type, +0xC ammo (0xFFFF = infinite). */
#define PROFILE_PLAYER_TABLE 0x10C718u
/* Player objects (pickup routine 0xC0928: +0x94 weapon flag, +0x95 cleared). */
#define PROFILE_PLAYER1_OBJECT 0x1014C4u
#define PROFILE_PLAYER2_OBJECT 0x101574u
#define PROFILE_OBJECT_CHARACTER 0x6F        /* internal character: Marco 0, Tarma 1, Eri 2, Fio 3 */
/* Player status objects 0x101204 / 0x1012B4: +0x83 soldier (0 Marco, 1 Eri, 2 Tarma, 3 Fio,
 * soldier select order), +0x82 the same as internal character, +0x84 lives (from the HERO
 * soft dip, 0xC942C). */
#define PROFILE_PLAYER1_SOLDIER 0x101287u
#define PROFILE_PLAYER2_SOLDIER 0x101337u
#define PROFILE_PLAYER1_LIVES 0x101288u
#define PROFILE_PLAYER2_LIVES 0x101338u
/* Weapon tables in the P ROM, indexed by weapon (0-15), used by the pickup routine. */
#define PROFILE_ROM_WEAPON_SUBTYPE 0xC08A8u  /* bytes */
#define PROFILE_ROM_WEAPON_AMMO 0xC08B8u     /* words: ammo on pickup */
#define PROFILE_ROM_WEAPON_FLAG 0xC0918u     /* bytes: player object +0x94 */
#define PROFILE_BLOOD_WORD 0x10FD8Eu
#define PROFILE_BLOOD_MASK 0x00FFu

/* Title screen logo (attract, frame ~1150 from power-on with empty SRAM):
 * sprites 41-124 (shadow, "METAL SLUG", X, "SUPER VEHICLE-001") and 380 ("TM", first tile 0x309CA,
 * used to recognize the moment). The red background is sprites 21-40. */
#define PROFILE_LOGO_FIRST 41
#define PROFILE_LOGO_LAST 124
#define PROFILE_LOGO_TM 380
#define PROFILE_LOGO_TM_TILE 0x309CAu
#define PROFILE_LOGO_MAX_FRAMES 3000

#endif
