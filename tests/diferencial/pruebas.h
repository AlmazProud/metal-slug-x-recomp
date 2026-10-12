#ifndef PRUEBAS_H
#define PRUEBAS_H

#include "cpu68k.h"

typedef struct {
    uint32_t dir;                 /* dirección de la instrucción en P1 */
    void (*fn)(Cpu68k *c);        /* la instrucción recompilada */
    const char *texto;            /* desensamblado (decodificador propio) */
} Prueba;

extern const Prueba pruebas[];
extern const int n_pruebas;

#endif
