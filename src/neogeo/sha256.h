#ifndef SHA256_H
#define SHA256_H

#include <stddef.h>
#include <stdint.h>

void sha256(const uint8_t *datos, size_t n, uint8_t salida[32]);

#endif
