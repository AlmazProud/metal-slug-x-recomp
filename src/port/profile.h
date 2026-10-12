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
