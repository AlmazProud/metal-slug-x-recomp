#ifndef TESTS_H
#define TESTS_H

#include "cpu68k.h"

typedef struct {
    uint32_t dir;                 /* dirección de la instrucción en P1 */
    void (*fn)(Cpu68k *c);        /* la instrucción recompilada */
    const char *text;            /* desensamblado (decodificador propio) */
} Test;

extern const Test tests[];
extern const int n_tests;

#endif
