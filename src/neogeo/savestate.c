/* Save states: the whole machine in a buffer, between frames.
 *
 * Format: "PDNGEST" + version (uint32) + the contents of each module in a fixed order (structs
 * as-is, plus the ymfm state). Only valid for the same executable: if the version or the size
 * changes, the state is rejected without touching the machine.
 */
#include "neogeo.h"

#include <stdlib.h>
#include <string.h>

#define MAGIC "PDNGEST"
#define VERSION 5u

void ng_state_block(NgState *e, void *p, size_t n)
{
    if (e->data && e->pos + n <= e->size) {
        if (e->saving) memcpy(e->data + e->pos, p, n);
        else memcpy(p, e->data + e->pos, n);
    }
    e->pos += n;
}

static void walk(NgState *e)
{
    char magic[8] = MAGIC;
    uint32_t version = VERSION;
    ng_state_block(e, magic, sizeof magic);
    ng_state_block(e, &version, sizeof version);
    ng_machine_state(e);
    ng_rtc_state(e);
    ng_sound_state(e);
}

uint8_t *ng_state_save(size_t *size)
{
    NgState e = {NULL, 0, 0, 1};
    walk(&e);                               /* first pass: only measures */
    e.size = e.pos;
    e.pos = 0;
    e.data = malloc(e.size);
    if (!e.data) return NULL;
    walk(&e);
    *size = e.size;
    return e.data;
}

int ng_state_load(const uint8_t *data, size_t size)
{
    NgState measure = {NULL, 0, 0, 1};
    walk(&measure);
    uint32_t version;
    if (size != measure.pos || size < 12 || memcmp(data, MAGIC, 8) != 0) return -1;
    memcpy(&version, data + 8, sizeof version);
    if (version != VERSION) return -1;
    NgState e = {(uint8_t *)data, size, 0, 0};
    walk(&e);
    return 0;
}
