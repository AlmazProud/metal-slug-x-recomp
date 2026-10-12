/* Datos propios de Metal Slug X que usa el port, encontrados en el análisis (listado de P1) y
 * confirmados con la partida grabada de la Misión 1. Primer paso hacia un "perfil de juego".
 */
#ifndef PERFIL_H
#define PERFIL_H

/* Bucle principal: espera la VBlank en 0x39B0-0x3A3E (bclr #7,-$7F75(a5); beq) y sale en 0x3A42. */
#define PERFIL_ESPERA_INI 0x39B0u
#define PERFIL_ESPERA_FIN 0x3A42u

/* Al salir de la espera, el juego copia en -$7F76(a5) = 0x10008A cuántas VBlanks duró el ciclo de
 * lógica: 2 normalmente (30 Hz), 3 o 4 con la ralentización (20 o 15 Hz). Offset en la RAM. */
#define PERFIL_RAM_CUADROS_LOGICA 0x008Au

#endif
