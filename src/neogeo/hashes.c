/* CRC-32 (IEEE 802.3, reflected, as zip and MAME use) and SHA-1 (FIPS 180-4). */
#include "hashes.h"

#include <stdio.h>
#include <string.h>

uint32_t crc32_update(uint32_t crc, const uint8_t *data, size_t n)
{
    static uint32_t table[256];
    if (!table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    crc = ~crc;
    for (size_t i = 0; i < n; i++) crc = table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static uint32_t rol(uint32_t x, int n) { return x << n | x >> (32 - n); }

static void sha1_block(uint32_t h[5], const uint8_t *p)
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[4 * i] << 24 | (uint32_t)p[4 * i + 1] << 16 | (uint32_t)p[4 * i + 2] << 8 | p[4 * i + 3];
    for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);          k = 0x5A827999u; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1u; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDCu; }
        else             { f = b ^ c ^ d;                   k = 0xCA62C1D6u; }
        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1(const uint8_t *data, size_t n, uint8_t output[20])
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};
    size_t i = 0;
    for (; i + 64 <= n; i += 64) sha1_block(h, data + i);
    uint8_t last[128] = {0};
    size_t rest = n - i;
    memcpy(last, data + i, rest);
    last[rest] = 0x80;
    size_t blocks = rest < 56 ? 1 : 2;
    uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) last[blocks * 64 - 1 - k] = (uint8_t)(bits >> (8 * k));
    for (size_t b = 0; b < blocks; b++) sha1_block(h, last + 64 * b);
    for (int k = 0; k < 5; k++) {
        output[4 * k] = (uint8_t)(h[k] >> 24); output[4 * k + 1] = (uint8_t)(h[k] >> 16);
        output[4 * k + 2] = (uint8_t)(h[k] >> 8); output[4 * k + 3] = (uint8_t)h[k];
    }
}

void hex_string(const uint8_t *data, int n, char *out)
{
    for (int i = 0; i < n; i++) snprintf(out + 2 * i, 3, "%02x", data[i]);
    out[2 * n] = 0;
}
