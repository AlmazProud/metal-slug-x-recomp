/* Minimal zip reader for the user's MAME sets (mslugx.zip, neogeo.zip): the central directory,
 * "stored" and "deflate" members, CRC-32 checked. No zip64, no encryption (MAME sets use
 * neither). */
#ifndef UNZIP_H
#define UNZIP_H

#include <stdint.h>

typedef struct {
    char name[256];          /* full name inside the zip (may contain folders) */
    uint32_t size;           /* uncompressed */
    uint32_t csize;
    uint32_t crc;
    uint32_t offset;         /* local header */
    uint16_t method;         /* 0 stored, 8 deflate */
} ZipEntry;

/* Reads the central directory. Returns the number of members (and a malloc'd array in
 * *entries) or -1 if the file is not a zip we can read. */
int zip_list(const char *path, ZipEntry **entries);

/* Extracts one member (malloc'd buffer of e->size bytes), or NULL on error / bad CRC. */
uint8_t *zip_read(const char *path, const ZipEntry *e);

/* Raw deflate (RFC 1951): returns 0 when exactly `out_size` bytes were produced. */
int inflate_raw(const uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t out_size);

#endif
