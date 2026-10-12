#ifndef TESTS_H
#define TESTS_H

#include "cpu68k.h"

typedef struct {
    uint32_t addr;                /* address of the instruction in P1 */
    void (*fn)(Cpu68k *c);        /* the recompiled instruction */
    const char *text;             /* disassembly (our own decoder) */
} Test;

extern const Test tests[];
extern const int n_tests;

#endif
