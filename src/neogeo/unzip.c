/* Minimal zip reader (see unzip.h) with its own inflate, written from RFC 1951 (DEFLATE) and
 * PKWARE's APPNOTE (zip container). Huffman codes are decoded with a 9-bit lookup table and a
 * canonical bit-by-bit fallback for longer codes. */
#include "unzip.h"
#include "hashes.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ inflate */
#define FAST_BITS 9

typedef struct {
    const uint8_t *in;
    uint32_t in_size, pos;
    uint32_t bitbuf;
    int bitcnt;
    uint8_t *out;
    uint32_t out_size, out_pos;
    int error;
} Inflate;

typedef struct {
    uint16_t count[16];          /* codes per length */
    uint16_t symbol[320];        /* symbols ordered by code */
    uint16_t fast[1 << FAST_BITS];   /* (symbol << 4) | length, 0 = slow path */
} Huffman;

/* Bits past the end of the input read as zero; the final position check catches a real overrun. */
static void need(Inflate *s, int n)
{
    while (s->bitcnt < n) {
        uint32_t b = s->pos < s->in_size ? s->in[s->pos] : 0;
        s->pos++;
        s->bitbuf |= b << s->bitcnt;
        s->bitcnt += 8;
    }
}

static int bits(Inflate *s, int n)
{
    if (n == 0) return 0;
    need(s, n);
    int v = (int)(s->bitbuf & ((1u << n) - 1));
    s->bitbuf >>= n;
    s->bitcnt -= n;
    return v;
}

static int build(Huffman *h, const uint8_t *lengths, int n)
{
    uint16_t offs[16];
    memset(h->count, 0, sizeof h->count);
    memset(h->fast, 0, sizeof h->fast);
    for (int i = 0; i < n; i++) h->count[lengths[i]]++;
    h->count[0] = 0;
    int left = 1;
    for (int len = 1; len < 16; len++) {
        left <<= 1;
        left -= h->count[len];
        if (left < 0) return -1;                    /* over-subscribed */
    }
    offs[1] = 0;
    for (int len = 1; len < 15; len++) offs[len + 1] = (uint16_t)(offs[len] + h->count[len]);
    for (int i = 0; i < n; i++) if (lengths[i]) h->symbol[offs[lengths[i]]++] = (uint16_t)i;
    /* Lookup table: canonical codes, bit-reversed because the stream is read LSB first. */
    int code = 0, index = 0;
    for (int len = 1; len <= FAST_BITS; len++) {
        for (int k = 0; k < h->count[len]; k++, code++, index++) {
            int r = 0;
            for (int b = 0; b < len; b++) r |= ((code >> b) & 1) << (len - 1 - b);
            for (int fill = r; fill < (1 << FAST_BITS); fill += 1 << len)
                h->fast[fill] = (uint16_t)(h->symbol[index] << 4 | len);
        }
        code <<= 1;
    }
    return 0;
}

static int decode(Inflate *s, const Huffman *h)
{
    need(s, 15);
    uint32_t b = s->bitbuf;
    uint16_t e = h->fast[b & ((1u << FAST_BITS) - 1)];
    if (e) {
        int len = e & 15;
        s->bitbuf >>= len;
        s->bitcnt -= len;
        return e >> 4;
    }
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= (int)((b >> (len - 1)) & 1);
        int count = h->count[len];
        if (code - count < first) {
            s->bitbuf >>= len;
            s->bitcnt -= len;
            return h->symbol[index + (code - first)];
        }
        index += count;
        first += count;
        first <<= 1;
        code <<= 1;
    }
    s->error = 1;
    return 0;
}

static const uint16_t LEN_BASE[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                      35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEN_EXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
                                      3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DIST_BASE[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                       257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
                                       8193, 12289, 16385, 24577};
static const uint8_t DIST_EXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
                                       7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int codes(Inflate *s, const Huffman *lit, const Huffman *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (s->error) return -1;
        if (sym < 256) {
            if (s->out_pos >= s->out_size) return -1;
            s->out[s->out_pos++] = (uint8_t)sym;
        } else if (sym == 256) {
            return 0;
        } else {
            sym -= 257;
            if (sym >= 29) return -1;
            uint32_t len = LEN_BASE[sym] + (uint32_t)bits(s, LEN_EXTRA[sym]);
            int d = decode(s, dist);
            if (s->error || d >= 30) return -1;
            uint32_t back = DIST_BASE[d] + (uint32_t)bits(s, DIST_EXTRA[d]);
            if (back > s->out_pos || len > s->out_size - s->out_pos) return -1;
            uint8_t *o = s->out + s->out_pos;
            for (uint32_t i = 0; i < len; i++) o[i] = o[(int64_t)i - back];
            s->out_pos += len;
        }
    }
}

static int stored(Inflate *s)
{
    s->bitbuf >>= s->bitcnt & 7;                     /* to a byte boundary */
    s->bitcnt -= s->bitcnt & 7;
    uint32_t len = (uint32_t)bits(s, 16);
    uint32_t nlen = (uint32_t)bits(s, 16);
    if (len != (~nlen & 0xFFFF) || len > s->out_size - s->out_pos) return -1;
    for (uint32_t i = 0; i < len; i++) {
        if (s->bitcnt >= 8) { s->out[s->out_pos++] = (uint8_t)s->bitbuf; s->bitbuf >>= 8; s->bitcnt -= 8; }
        else {
            if (s->pos >= s->in_size) return -1;
            s->out[s->out_pos++] = s->in[s->pos++];
        }
    }
    return 0;
}

static int fixed(Inflate *s)
{
    static Huffman lit, dist;
    static int ready;
    if (!ready) {
        uint8_t l[288];
        int i = 0;
        for (; i < 144; i++) l[i] = 8;
        for (; i < 256; i++) l[i] = 9;
        for (; i < 280; i++) l[i] = 7;
        for (; i < 288; i++) l[i] = 8;
        build(&lit, l, 288);
        for (i = 0; i < 30; i++) l[i] = 5;
        build(&dist, l, 30);
        ready = 1;
    }
    return codes(s, &lit, &dist);
}

static int dynamic(Inflate *s)
{
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
    Huffman lencode, lit, dist;
    uint8_t lengths[320];
    int nlen = bits(s, 5) + 257, ndist = bits(s, 5) + 1, ncode = bits(s, 4) + 4;
    if (nlen > 286 || ndist > 30) return -1;
    memset(lengths, 0, sizeof lengths);
    for (int i = 0; i < ncode; i++) lengths[ORDER[i]] = (uint8_t)bits(s, 3);
    if (build(&lencode, lengths, 19) != 0) return -1;
    int i = 0;
    while (i < nlen + ndist) {
        int sym = decode(s, &lencode);
        if (s->error) return -1;
        if (sym < 16) { lengths[i++] = (uint8_t)sym; continue; }
        int value = 0, repeat;
        if (sym == 16) {
            if (i == 0) return -1;
            value = lengths[i - 1];
            repeat = 3 + bits(s, 2);
        } else if (sym == 17) repeat = 3 + bits(s, 3);
        else repeat = 11 + bits(s, 7);
        if (i + repeat > nlen + ndist) return -1;
        while (repeat--) lengths[i++] = (uint8_t)value;
    }
    if (lengths[256] == 0) return -1;                /* no end-of-block code */
    if (build(&lit, lengths, nlen) != 0 || build(&dist, lengths + nlen, ndist) != 0) return -1;
    return codes(s, &lit, &dist);
}

int inflate_raw(const uint8_t *in, uint32_t in_size, uint8_t *out, uint32_t out_size)
{
    Inflate s = {in, in_size, 0, 0, 0, out, out_size, 0, 0};
    int last;
    do {
        last = bits(&s, 1);
        int type = bits(&s, 2);
        int r = type == 0 ? stored(&s) : type == 1 ? fixed(&s) : type == 2 ? dynamic(&s) : -1;
        if (r != 0 || s.error) return -1;
    } while (!last);
    if (s.pos - (uint32_t)(s.bitcnt / 8) > in_size) return -1;      /* read past the input */
    return s.out_pos == out_size ? 0 : -1;
}

/* ------------------------------------------------------------------ zip container */
static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

int zip_list(const char *path, ZipEntry **entries)
{
    *entries = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long size = ftell(f);
    long tail = size < 65557 ? size : 65557;          /* end record + longest comment */
    uint8_t *t = malloc((size_t)tail);
    int n = -1;
    if (t && tail >= 22 && fseek(f, size - tail, SEEK_SET) == 0 && fread(t, 1, (size_t)tail, f) == (size_t)tail) {
        for (long i = tail - 22; i >= 0; i--) {
            if (le32(t + i) != 0x06054B50u) continue;
            uint32_t count = le16(t + i + 10), cd_size = le32(t + i + 12), cd_offset = le32(t + i + 16);
            uint8_t *cd = malloc(cd_size ? cd_size : 1);
            ZipEntry *e = calloc(count ? count : 1, sizeof *e);
            if (cd && e && fseek(f, (long)cd_offset, SEEK_SET) == 0 && fread(cd, 1, cd_size, f) == cd_size) {
                uint32_t p = 0;
                n = 0;
                for (uint32_t k = 0; k < count; k++) {
                    if (p + 46 > cd_size || le32(cd + p) != 0x02014B50u) { n = -1; break; }
                    uint32_t name_len = le16(cd + p + 28), extra = le16(cd + p + 30), comment = le16(cd + p + 32);
                    if (p + 46 + name_len > cd_size) { n = -1; break; }
                    ZipEntry *z = &e[n];
                    uint32_t copy = name_len < sizeof z->name - 1 ? name_len : (uint32_t)sizeof z->name - 1;
                    memcpy(z->name, cd + p + 46, copy);
                    z->name[copy] = 0;
                    z->method = (uint16_t)le16(cd + p + 10);
                    z->crc = le32(cd + p + 16);
                    z->csize = le32(cd + p + 20);
                    z->size = le32(cd + p + 24);
                    z->offset = le32(cd + p + 42);
                    if (copy && z->name[copy - 1] != '/') n++;          /* skip folders */
                    p += 46 + name_len + extra + comment;
                }
            }
            free(cd);
            if (n >= 0) *entries = e; else free(e);
            break;
        }
    }
    free(t);
    fclose(f);
    return n;
}

uint8_t *zip_read(const char *path, const ZipEntry *e)
{
    if (e->method != 0 && e->method != 8) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t h[30];
    uint8_t *packed = NULL, *out = NULL;
    if (fseek(f, (long)e->offset, SEEK_SET) == 0 && fread(h, 1, 30, f) == 30 && le32(h) == 0x04034B50u &&
        fseek(f, (long)(e->offset + 30 + le16(h + 26) + le16(h + 28)), SEEK_SET) == 0) {
        packed = malloc(e->csize ? e->csize : 1);
        out = malloc(e->size ? e->size : 1);
        int ok = packed && out && fread(packed, 1, e->csize, f) == e->csize;
        if (ok && e->method == 0) ok = e->csize == e->size && (memcpy(out, packed, e->size), 1);
        else if (ok) ok = inflate_raw(packed, e->csize, out, e->size) == 0;
        if (ok) ok = crc32_update(0, out, e->size) == e->crc;
        if (!ok) { free(out); out = NULL; }
    }
    free(packed);
    fclose(f);
    return out;
}
