/* Comparable 68000 state, shared by Musashi and the recompiled code. */
#ifndef STATE_H
#define STATE_H

#include <stdint.h>

typedef struct {
    uint32_t d[8];
    uint32_t a[8];     /* a[7] = active stack pointer */
    uint32_t usp, ssp; /* both stack pointers (the active one matches a[7]) */
    uint32_t sr;
    uint32_t pc;
} State;

#endif
