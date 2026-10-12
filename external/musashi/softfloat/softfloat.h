/* Projecto Decompile: sustituto de softfloat/softfloat.h para compilar Musashi SIN SoftFloat.
 * Solo define el tipo de los registros FPU que aparece en la estructura de la CPU de Musashi
 * (m68kcpu.h). Ninguna operación de coma flotante se emula: el 68000 no tiene FPU.
 * Licencia: la del proyecto. No contiene código de SoftFloat. */
#ifndef PD_SOFTFLOAT_H
#define PD_SOFTFLOAT_H

typedef struct {
    uint16 high;
    uint64 low;
} floatx80;

#endif
