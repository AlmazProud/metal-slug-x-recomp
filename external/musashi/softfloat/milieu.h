/* Projecto Decompile: replacement for softfloat/milieu.h so Musashi builds WITHOUT SoftFloat.
 * The 68000 has no FPU; Musashi only needs these integer type names to exist (m68kcpu.h
 * includes them before defining the CPU, and m68kops.c uses int8/int32).
 * License: the project's. Contains no SoftFloat code. */
#ifndef PD_MILIEU_H
#define PD_MILIEU_H

typedef sint8  flag;
typedef sint8  int8;
typedef sint16 int16;
typedef sint32 int32;
typedef sint64 int64;

#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#endif
