/* Projecto Decompile: sustituto de softfloat/milieu.h para compilar Musashi SIN SoftFloat.
 * El 68000 no tiene FPU; Musashi solo necesita que existan estos nombres de tipos enteros
 * (m68kcpu.h los incluye antes de definir la CPU, y m68kops.c usa int8/int32).
 * Licencia: la del proyecto. No contiene código de SoftFloat. */
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
