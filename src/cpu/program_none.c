/* Build without a recompiled program (PD_RECOMPILED=OFF, or no ROMs / analysis at build time):
 * the 68000 runs entirely in the interpreter. This is the build that can be distributed, since
 * it contains no code derived from the game. */
const char program_p1_sha256[] = "";
const char program_bios_sha256[] = "";

void program_register(void) {}
