/* Projecto Decompile: replacement for m68kfpu.c so Musashi builds WITHOUT SoftFloat.
 *
 * m68kcpu.c includes this file. The 68000 has no FPU: coprocessor instructions (opcodes
 * 1111xxxx) raise the Line-F exception, as on real hardware.
 * m68kmmu.h uses the original READ_EA_* / WRITE_EA_* accessors; with the PMMU disabled
 * (M68K_EMULATE_PMMU=OFF) they never run, so here they abort.
 * License: the project's. Contains no code from SoftFloat or from the original m68kfpu.c. */
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

static uint32 READ_EA_32(int ea)  { (void)ea; fatalerror("PD: READ_EA_32 without FPU/PMMU\n"); return 0; }
static uint64 READ_EA_64(int ea)  { (void)ea; fatalerror("PD: READ_EA_64 without FPU/PMMU\n"); return 0; }
static void WRITE_EA_32(int ea, uint32 data) { (void)ea; (void)data; fatalerror("PD: WRITE_EA_32 without FPU/PMMU\n"); }
static void WRITE_EA_64(int ea, uint64 data) { (void)ea; (void)data; fatalerror("PD: WRITE_EA_64 without FPU/PMMU\n"); }

void m68040_fpu_op0(void) { m68ki_exception_1111(); }
void m68040_fpu_op1(void) { m68ki_exception_1111(); }
