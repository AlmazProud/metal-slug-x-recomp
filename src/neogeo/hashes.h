/* CRC-32 (zip / MAME) and SHA-1 (MAME's ROM hashes). SHA-256 lives in sha256.h. */
#ifndef HASHES_H
#define HASHES_H

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t n);   /* start with 0 */
void sha1(const uint8_t *data, size_t n, uint8_t output[20]);

/* Lowercase hex of `n` bytes into `out` (2n + 1 chars). */
void hex_string(const uint8_t *data, int n, char *out);

#endif
