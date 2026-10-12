/* State comparable del 68000, común a Musashi y al código recompilado. */
#ifndef STATE_H
#define STATE_H

#include <stdint.h>

typedef struct {
    uint32_t d[8];
    uint32_t a[8];     /* a[7] = puntero de pila active */
    uint32_t usp, ssp; /* ambos punteros (el active coincide con a[7]) */
    uint32_t sr;
    uint32_t pc;
} State;

#endif
