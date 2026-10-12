/* Estados guardados: la máquina completa en un búfer, entre frames.
 *
 * Formato: "PDNGEST" + versión (uint32) + el contenido de cada módulo en orden fijo (estructuras
 * tal cual, más el estado de ymfm). Solo sirve para el mismo ejecutable: si cambia la versión o
 * el tamaño, el estado se rechaza sin tocar la máquina.
 */
#include "neogeo.h"

#include <stdlib.h>
#include <string.h>

#define MAGIA "PDNGEST"
#define VERSION 1u

void ng_state_block(NgState *e, void *p, size_t n)
{
    if (e->data && e->pos + n <= e->tam) {
        if (e->saving) memcpy(e->data + e->pos, p, n);
        else memcpy(p, e->data + e->pos, n);
    }
    e->pos += n;
}

static void recorrer(NgState *e)
{
    char magia[8] = MAGIA;
    uint32_t version = VERSION;
    ng_state_block(e, magia, sizeof magia);
    ng_state_block(e, &version, sizeof version);
    ng_machine_state(e);
    ng_rtc_state(e);
    ng_sound_state(e);
}

uint8_t *ng_state_save(size_t *tam)
{
    NgState e = {NULL, 0, 0, 1};
    recorrer(&e);                           /* primera pasada: solo mide */
    e.tam = e.pos;
    e.pos = 0;
    e.data = malloc(e.tam);
    if (!e.data) return NULL;
    recorrer(&e);
    *tam = e.tam;
    return e.data;
}

int ng_state_load(const uint8_t *data, size_t tam)
{
    NgState medida = {NULL, 0, 0, 1};
    recorrer(&medida);
    uint32_t version;
    if (tam != medida.pos || tam < 12 || memcmp(data, MAGIA, 8) != 0) return -1;
    memcpy(&version, data + 8, sizeof version);
    if (version != VERSION) return -1;
    NgState e = {(uint8_t *)data, tam, 0, 0};
    recorrer(&e);
    return 0;
}
