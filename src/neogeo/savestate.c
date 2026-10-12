/* Estados guardados: la máquina completa en un búfer, entre cuadros.
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

void ng_estado_bloque(NgEstado *e, void *p, size_t n)
{
    if (e->datos && e->pos + n <= e->tam) {
        if (e->guardando) memcpy(e->datos + e->pos, p, n);
        else memcpy(p, e->datos + e->pos, n);
    }
    e->pos += n;
}

static void recorrer(NgEstado *e)
{
    char magia[8] = MAGIA;
    uint32_t version = VERSION;
    ng_estado_bloque(e, magia, sizeof magia);
    ng_estado_bloque(e, &version, sizeof version);
    ng_maquina_estado(e);
    ng_rtc_estado(e);
    ng_sonido_estado(e);
}

uint8_t *ng_estado_guardar(size_t *tam)
{
    NgEstado e = {NULL, 0, 0, 1};
    recorrer(&e);                           /* primera pasada: solo mide */
    e.tam = e.pos;
    e.pos = 0;
    e.datos = malloc(e.tam);
    if (!e.datos) return NULL;
    recorrer(&e);
    *tam = e.tam;
    return e.datos;
}

int ng_estado_cargar(const uint8_t *datos, size_t tam)
{
    NgEstado medida = {NULL, 0, 0, 1};
    recorrer(&medida);
    uint32_t version;
    if (tam != medida.pos || tam < 12 || memcmp(datos, MAGIA, 8) != 0) return -1;
    memcpy(&version, datos + 8, sizeof version);
    if (version != VERSION) return -1;
    NgEstado e = {(uint8_t *)datos, tam, 0, 0};
    recorrer(&e);
    return 0;
}
