/* Projecto Decompile: replacement for softfloat/softfloat.h so Musashi builds WITHOUT SoftFloat.
 * It only defines the FPU register type that appears in Musashi's CPU struct (m68kcpu.h). No
 * floating-point operation is emulated: the 68000 has no FPU.
 * License: the project's. Contains no SoftFloat code. */
#ifndef PD_SOFTFLOAT_H
#define PD_SOFTFLOAT_H

typedef struct {
    uint16 high;
    uint64 low;
} floatx80;

#endif
