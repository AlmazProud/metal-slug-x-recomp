/* Projecto Decompile: sustituto de m68kfpu.c para compilar Musashi SIN SoftFloat.
 *
 * m68kcpu.c incluye este archivo. El 68000 no tiene FPU: las instrucciones de coprocesador
 * (opcodes 1111xxxx) generan la excepción Line-F, igual que en el hardware real.
 * m68kmmu.h usa los accesos READ_EA_* / WRITE_EA_* de la versión original; con la PMMU
 * desactivada (M68K_EMULATE_PMMU=OFF) nunca se ejecutan, así que aquí abortan.
 * Licencia: la del proyecto. No contiene código de SoftFloat ni del m68kfpu.c original. */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "m68kcpu.h"

static void fatalerror(const char *format, ...)
{
    va_list ap;
    va_start(ap, format);
    vfprintf(stderr, format, ap);
    va_end(ap);
    abort();
}

static uint32 READ_EA_32(int ea)  { (void)ea; fatalerror("PD: READ_EA_32 sin FPU/PMMU\n"); return 0; }
static uint64 READ_EA_64(int ea)  { (void)ea; fatalerror("PD: READ_EA_64 sin FPU/PMMU\n"); return 0; }
static void WRITE_EA_32(int ea, uint32 data) { (void)ea; (void)data; fatalerror("PD: WRITE_EA_32 sin FPU/PMMU\n"); }
static void WRITE_EA_64(int ea, uint64 data) { (void)ea; (void)data; fatalerror("PD: WRITE_EA_64 sin FPU/PMMU\n"); }

void m68040_fpu_op0(void) { m68ki_exception_1111(); }
void m68040_fpu_op1(void) { m68ki_exception_1111(); }
