/* Datos propios de Metal Slug X que usa el port, encontrados en el análisis (listado de P1) y
 * confirmados con la partida grabada de la Misión 1. Primer paso hacia un "perfil de juego".
 */
#ifndef PROFILE_H
#define PROFILE_H

/* Bucle principal: espera la VBlank en 0x39B0-0x3A3E (bclr #7,-$7F75(a5); beq) y sale en 0x3A42. */
#define PROFILE_WAIT_START 0x39B0u
#define PROFILE_WAIT_END 0x3A42u

/* Al salir de la espera, el juego copia en -$7F76(a5) = 0x10008A cuántas VBlanks duró el ciclo de
 * lógica: 2 normalmente (30 Hz), 3 o 4 con la ralentización (20 o 15 Hz). Offset en la RAM. */
#define PROFILE_RAM_FRAMES_PER_TICK 0x008Au

/* Sangre: las opciones del juego en el menú de service del BIOS (soft dips, tabla de la cabecera
 * del P ROM) incluyen "BLOOD OFF/ON"; en EE. UU. y Europa viene en OFF (blood blanca). El BIOS
 * copia las opciones a 0x10FD84: dos tiempos, dos cuentas y diez opciones desde 0x10FD8A
 * (CONTINUE, DIFFICULTY, PLAY TIME, DEMO SOUND, PLAY MANUAL, BLOOD...). El juego la consulta en
 * 0x1B2BE (tst.b $10FD8F). Escribir ese byte desde el encendido rompe el test de RAM del BIOS: se
 * fuerza solo la lectura del código del juego (ng_forced_read, byte bajo de la palabra). */
/* Dips de depuración del juego: dos bytes que solo valen con el mode desarrollo del BIOS
 * (0x10FE80); si no, el juego los borra en cada cuadro (0x2592). Se fuerzan como la blood. */
#define PROFILE_DIPS_WORD 0x10F000
/* Modo desarrollo del BIOS (byte alto de la palabra): abre el menú oculto de los desarrolladores. */
#define PROFILE_DEVMODE_WORD 0x10FE80
#define PROFILE_BLOOD_WORD 0x10FD8Eu
#define PROFILE_BLOOD_MASK 0x00FFu

/* Logo de la screen de título (attract, cuadro ~1150 desde el encendido con la SRAM vacía):
 * sprites 41-124 (shadow, "METAL SLUG", X, "SUPER VEHICLE-001") y 380 ("TM", primer tile 0x309CA,
 * que sirve para reconocer el momento). El fondo rojo son los sprites 21-40. */
#define PROFILE_LOGO_FIRST 41
#define PROFILE_LOGO_LAST 124
#define PROFILE_LOGO_TM 380
#define PROFILE_LOGO_TM_TILE 0x309CAu
#define PROFILE_LOGO_MAX_FRAMES 3000

#endif
