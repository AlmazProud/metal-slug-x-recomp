/* LSPC: VRAM, registros, palette y dibujo por línea de sprites y layer fix.
 *
 * VRAM (palabras):
 *   0000-6FFF  SCB1: 64 palabras por sprite (32 tiles: código bajo, atributos)
 *   7000-74FF  layer fix: 40 columnas x 32 filas, por columnas; palette en bits 15-12
 *   8000-81FF  SCB2: reducción (bits 11-8 horizontal, 7-0 vertical)
 *   8200-83FF  SCB3: Y (bits 15-7), encadenado (bit 6), alto en tiles (bits 5-0)
 *   8400-85FF  SCB4: X (bits 15-7)
 *   8600-86FF  listas de sprites por línea (las escribe el propio LSPC, par/impar)
 *
 * Temporización: al comienzo de cada línea se dibuja la anterior (con la VRAM y la palette de ese
 * momento) y se arma la lista de sprites de la actual: hasta 96 de los 381 sprites.
 *
 * Los gráficos se leen directamente en el formato desempaquetado de DotEmu (4 bpp por filas,
 * nibble bajo = píxel izquierdo), equivalente bit a bit al de los C ROMs (docs/ROM_MAP.md).
 */
#include "neogeo.h"

#include <string.h>

#define MAX_SPRITES 381
#define MAX_POR_LINEA 96

/* Reducción horizontal: qué columnas del tile se dibujan para cada value (hardware). */
static const uint16_t ZOOM_X[16] = {
    0x0080, 0x0880, 0x0888, 0x2888, 0x288a, 0x2a8a, 0x2aaa, 0xaaaa,
    0xaaea, 0xbaea, 0xbaeb, 0xbbeb, 0xbbef, 0xfbef, 0xfbff, 0xffff,
};

/* ------------------------------------------------------------------ palette */
/* DAC de cada componente: 5 resistencias (3900, 2200, 1000, 470 y 220 ohm, del bit menos al más
 * significativo) en paralelo hacia la output; el bit "oscuro" agrega 8200 ohm a masa y la shadow
 * 150 ohm. Cada bit aporta la tensión del divisor que forma su resistencia (a Vcc) contra el
 * resto (a masa); la scale se fija para que el blanco normal sea 255. */
static uint8_t nivel[32][4];                 /* [value de 5 bits][normal, oscuro, shadow, ambos] */

static void calcular_pesos(double carga, double scale, double pesos[5])
{
    static const double R[5] = {3900, 2200, 1000, 470, 220};
    for (int n = 0; n < 5; n++) {
        double g_masa = carga > 0 ? 1.0 / carga : 1e-12, g_vcc = 1e-12;
        for (int j = 0; j < 5; j++) {
            if (j == n) g_vcc += 1.0 / R[j];
            else g_masa += 1.0 / R[j];
        }
        double r_masa = 1.0 / g_masa, r_vcc = 1.0 / g_vcc;
        double v = 255.0 * r_masa / (r_vcc + r_masa);
        pesos[n] = (v < 0 ? 0 : v > 255 ? 255 : v) * scale;
    }
}

static void crear_niveles(void)
{
    double p[4][5];
    calcular_pesos(0, 1.0, p[0]);
    double suma = 0;
    for (int n = 0; n < 5; n++) suma += p[0][n];
    double scale = 255.0 / suma;
    const double cargas[4] = {0, 8200, 150, 1.0 / (1.0 / 8200 + 1.0 / 150)};
    for (int k = 0; k < 4; k++) calcular_pesos(cargas[k], scale, p[k]);
    for (int i = 0; i < 32; i++)
        for (int k = 0; k < 4; k++) {
            double s = 0;
            for (int n = 0; n < 5; n++) if (i >> n & 1) s += p[k][n];
            nivel[i][k] = (uint8_t)(int)(s + 0.5);
        }
}

void ng_palette_write(uint32_t i, uint16_t v)
{
    ng.palette[i] = v;
    int oscuro = v >> 15;
    int r = ((v >> 14) & 1) | ((v >> 7) & 0x1E);
    int g = ((v >> 13) & 1) | ((v >> 3) & 0x1E);
    int b = ((v >> 12) & 1) | ((v << 1) & 0x1E);
    for (int s = 0; s < 2; s++) {
        int k = oscuro + 2 * s;
        ng.pen[s][i] = 0xFF000000u | (uint32_t)nivel[r][k] << 16 | (uint32_t)nivel[g][k] << 8 | nivel[b][k];
    }
}

/* ------------------------------------------------------------------ VRAM y registros */
static void fijar_dir(uint16_t v)
{
    ng.vram_addr = (v & 0x8000) ? (v & 0x87FF) : v;
    ng.vram_buffer = ng.vram[ng.vram_addr];           /* la lectura ocurre al fijar la dirección */
}

static void irq2_programar(void)
{
    ng.irq2_when = ng_cpu.cycles + ((int64_t)ng.irq2_counter + 1) * 2;   /* reloj de píxel = 6 MHz */
}

uint16_t ng_video_read(uint32_t reg)
{
    switch (reg) {
    case 0: case 1: return ng.vram_buffer;
    case 2: return ng.vram_mod;
    default: {
        /* Contador vertical: 0x100 + vpos, de 0xF8 a 0x1FF. */
        int64_t t = ng_cpu.cycles - ng.frame_start;
        int vpos = (int)(t / NG_CYCLES_PER_LINE);
        if (vpos >= NG_LINES) vpos = NG_LINES - 1;
        int v = vpos + 0x100;
        if (v >= 0x200) v -= NG_LINES;
        return (uint16_t)(v << 7 | (ng.anim_counter & 7));
    }
    }
}

void ng_video_write(uint32_t reg, uint16_t v)
{
    switch (reg) {
    case 0: fijar_dir(v); break;
    case 1:
        ng.vram[ng.vram_addr] = v;
        fijar_dir((uint16_t)((ng.vram_addr & 0x8000) | ((ng.vram_addr + ng.vram_mod) & 0x7FFF)));
        break;
    case 2: ng.vram_mod = v; break;
    case 3:
        ng.anim_speed = (uint8_t)(v >> 8);
        ng.anim_disabled = (v >> 3) & 1;
        ng.irq2_control = v & 0xF0;
        break;
    case 4: ng.irq2_counter = (ng.irq2_counter & 0xFFFF) | (uint32_t)v << 16; break;
    case 5:
        ng.irq2_counter = (ng.irq2_counter & 0xFFFF0000u) | v;
        if (ng.irq2_control & 0x20) irq2_programar();          /* carga relativa */
        break;
    case 6:
        if (v & 1) ng.irq3 = 0;
        if (v & 2) ng.irq_raster = 0;
        if (v & 4) {
            ng.irq_vblank = 0;
            if (ng_on_vblank_ack) ng_on_vblank_ack();
        }
        ng_irq_update();
        break;
    default: break;                                            /* 7: solo PAL */
    }
}

void ng_video_anim_tick(void)
{
    if (ng.anim_frames == 0) {
        ng.anim_frames = ng.anim_speed;
        ng.anim_counter++;
    } else {
        ng.anim_frames--;
    }
}

/* ------------------------------------------------------------------ sprites */
static int en_linea(int line, int y, int filas)
{
    return filas == 0 || filas >= 0x20 || ((line - y) & 0x1FF) < filas * 0x10;
}

static void armar_lista(int line)
{
    uint16_t *lista = &ng.vram[(line & 1) ? 0x8680 : 0x8600];
    int y = 0, filas = 0, n = 0;
    for (int s = 0; s < MAX_SPRITES && n < MAX_POR_LINEA; s++) {
        uint16_t ctl = ng.vram[0x8200 | s];
        if (!(ctl & 0x40)) {
            y = 0x200 - (ctl >> 7);
            filas = ctl & 0x3F;
        }
        if (filas == 0 || !en_linea(line, y, filas)) continue;
        lista[n++] = (uint16_t)s;
    }
    /* el resto en 0, con una entrada extra */
    for (int i = n; i <= MAX_POR_LINEA && (lista + i) < ng.vram + 0x8800; i++) lista[i] = 0;
}

/* Píxel `col` (0-15, de izquierda a derecha en screen) de una fila de tile. En el archivo de
 * DotEmu cada mitad de 8 píxeles está guardada en orden inverso respecto de la screen (el bit
 * 7 - x de los planos del C ROM es el píxel x de la mitad, ver docs/ROM_MAP.md), de ahí col ^ 7. */
static inline uint8_t pixel_sprite(uint32_t codigo, int fila, int col)
{
    col ^= 7;
    const uint8_t *t = ng_roms.tiles + (size_t)codigo * 128 + fila * 8 + (col >> 1);
    return (col & 1) ? (*t >> 4) : (*t & 0x0F);
}

static void dibujar_lista(uint32_t *buf, const uint32_t *pens, int line, const uint16_t *lista, int ultimo);

static void dibujar_sprites(uint32_t *buf, const uint32_t *pens, int line)
{
    const uint16_t *lista = &ng.vram[(line & 1) ? 0x8680 : 0x8600];
    int ultimo;
    for (ultimo = MAX_POR_LINEA - 1; ultimo >= 0; ultimo--)
        if (lista[ultimo] != 0) break;
    /* La lista termina en ceros, pero un 0 puede ser el sprite 0: se dibuja uno más. */
    if (ultimo != MAX_POR_LINEA - 1) ultimo++;
    dibujar_lista(buf, pens, line, lista, ultimo);
}

/* Dibuja los sprites lista[0..ultimo] en la línea (los encadenados siguen al anterior de la lista). */
static void dibujar_lista(uint32_t *buf, const uint32_t *pens, int line, const uint16_t *lista, int ultimo)
{

    int x = 0, y = 0, filas = 0, zoom_x = 0, zoom_y = 0;
    for (int i = 0; i <= ultimo; i++) {
        int s = lista[i] & 0x1FF;
        uint16_t ctl_y = ng.vram[0x8200 | s];
        uint16_t ctl_zoom = ng.vram[0x8000 | s];
        if (ctl_y & 0x40) {                           /* encadenado: sigue a la derecha */
            x = (x + zoom_x + 1) & 0x1FF;
            zoom_x = (ctl_zoom >> 8) & 0x0F;
        } else {
            y = 0x200 - (ctl_y >> 7);
            x = ng.vram[0x8400 | s] >> 7;
            zoom_y = ctl_zoom & 0xFF;
            zoom_x = (ctl_zoom >> 8) & 0x0F;
            filas = ctl_y & 0x3F;
        }
        if (x >= 0x140 && x <= 0x1F0) continue;       /* fuera de la screen */
        if (!en_linea(line, y, filas)) continue;

        int linea_spr = (line - y) & 0x1FF;
        int linea_zoom = linea_spr & 0xFF;
        int invertir = (linea_spr >> 8) & 1;
        if (invertir) linea_zoom ^= 0xFF;
        if (filas > 0x20) {                           /* sprites de más de 32 tiles: repetición */
            linea_zoom %= (zoom_y + 1) << 1;
            if (linea_zoom > zoom_y) {
                linea_zoom = ((zoom_y + 1) << 1) - 1 - linea_zoom;
                invertir = !invertir;
            }
        }
        uint8_t yt = ng_roms.zoom[zoom_y << 8 | linea_zoom];
        int fila_tile = yt & 0x0F, tile = yt >> 4;
        if (invertir) { fila_tile ^= 0x0F; tile ^= 0x1F; }

        uint32_t dir = (uint32_t)s << 6 | (uint32_t)tile << 1;
        uint16_t attr = ng.vram[dir + 1];
        uint32_t codigo = ((uint32_t)(attr << 12) & 0xF0000) | ng.vram[dir];
        if (!ng.anim_disabled) {
            if (attr & 0x08) codigo = (codigo & ~7u) | (ng.anim_counter & 7);
            else if (attr & 0x04) codigo = (codigo & ~3u) | (ng.anim_counter & 3);
        }
        if (attr & 0x02) fila_tile ^= 0x0F;           /* volteo vertical */
        codigo &= 0x7FFFF;                            /* 64 MB direccionables */
        if (codigo >= ng_roms.n_tiles) continue;      /* fuera del ROM: transparente */

        if (ng_dbg.active) {                          /* box del sprite (mode debug) */
            NgBox *k = &ng_dbg.box_current[s];
            int x0 = x > 0x1F0 ? x - 0x200 : x, x1 = x0 + zoom_x;
            int yv = line - NG_FIRST_VISIBLE_LINE;
            if (k->x1 < k->x0) *k = (NgBox){(int16_t)x0, (int16_t)x1, (int16_t)yv, (int16_t)yv};
            else {
                if (x0 < k->x0) k->x0 = (int16_t)x0;
                if (x1 > k->x1) k->x1 = (int16_t)x1;
                if (yv < k->y0) k->y0 = (int16_t)yv;
                if (yv > k->y1) k->y1 = (int16_t)yv;
            }
        }
        const uint32_t *p = pens + ((attr >> 8) << 4);
        uint16_t mapa = ZOOM_X[zoom_x];
        int volteo = attr & 0x01;
        int envuelto = x > 0x1F0;                     /* empieza a la izquierda de la screen */
        int px = x;
        for (int col = 0; col < 16 && mapa; col++, mapa = (uint16_t)(mapa << 1)) {
            if (!(mapa & 0x8000)) continue;
            int pos = px++;
            if (envuelto && pos < 0x200) continue;
            int destino = pos & 0x1FF;
            uint8_t c = pixel_sprite(codigo, fila_tile, volteo ? 15 - col : col);
            if (c && destino < NG_WIDTH) buf[destino] = p[c];
        }
    }
}

/* ------------------------------------------------------------------ layer fix */
static void dibujar_fix(uint32_t *buf, const uint32_t *pens, int line)
{
    const uint8_t *gfx = ng.cart_fix ? ng_roms.fix_game : ng_roms.fix_bios;
    int fila = line >> 3, sub = line & 7;
    for (int col = 0; col < 40; col++) {
        uint16_t v = ng.vram[0x7000 | (col << 5) | (fila & 0x1F)];
        uint32_t codigo = v & 0x0FFF;
        const uint32_t *p = pens + ((v >> 12) << 4);
        const uint8_t *t = gfx + codigo * 32 + sub * 4;
        for (int k = 0; k < 4; k++) {
            uint8_t b = t[k];
            if (b & 0x0F) buf[col * 8 + 2 * k] = p[b & 0x0F];
            if (b & 0xF0) buf[col * 8 + 2 * k + 1] = p[b >> 4];
        }
    }
}

/* ------------------------------------------------------------------ líneas */
static void dibujar_linea(int line)
{
    if (line < NG_FIRST_VISIBLE_LINE || line >= NG_VBLANK_LINE) return;
    uint32_t *buf = ng.screen[line - NG_FIRST_VISIBLE_LINE];
    const uint32_t *pens = ng.pen[ng.shadow] + ((uint32_t)ng.palette_bank << 12);
    uint32_t fondo = pens[0xFFF];
    for (int i = 0; i < NG_WIDTH; i++) buf[i] = fondo;
    if (!(ng_dbg.hide & NG_HIDE_SPRITES)) dibujar_sprites(buf, pens, line);
    if (!(ng_dbg.hide & NG_HIDE_FIX)) dibujar_fix(buf, pens, line);
}

/* Fase 6C: redibuja solo los sprites [desde, hasta] con la VRAM y la palette actuales, sobre fondo
   transparente (0), sin tocar el estado de la máquina. Para usar gráficos del juego en los menús
   (el logo de la screen de título). */
void ng_video_capture_sprites(uint32_t *destino, int desde, int hasta)
{
    const uint32_t *pens = ng.pen[ng.shadow] + ((uint32_t)ng.palette_bank << 12);
    uint16_t lista[MAX_SPRITES];
    for (int y = 0; y < NG_HEIGHT; y++) {
        int line = y + NG_FIRST_VISIBLE_LINE, n = 0, yc = 0, filas = 0;
        for (int s = 0; s < MAX_SPRITES; s++) {
            uint16_t ctl = ng.vram[0x8200 | s];
            if (!(ctl & 0x40)) { yc = 0x200 - (ctl >> 7); filas = ctl & 0x3F; }
            if (s < desde || s > hasta || filas == 0 || !en_linea(line, yc, filas)) continue;
            lista[n++] = (uint16_t)s;
        }
        uint32_t *buf = destino + (size_t)y * NG_WIDTH;
        memset(buf, 0, NG_WIDTH * sizeof *buf);
        if (n) dibujar_lista(buf, pens, line, lista, n - 1);
    }
}

void ng_video_line_start(int line)
{
    if (line != 0) dibujar_linea(line - 1);
    armar_lista(line);
}

void ng_video_init(void)
{
    static int niveles_listos;
    if (!niveles_listos) { crear_niveles(); niveles_listos = 1; }
    memset(ng.vram, 0, sizeof ng.vram);
    for (uint32_t i = 0; i < 0x2000; i++) ng_palette_write(i, 0);
    ng.vram_addr = ng.vram_buffer = ng.vram_mod = 0;
    ng.anim_speed = ng.anim_disabled = ng.anim_counter = ng.anim_frames = 0;
    ng.irq2_control = 0;
    ng.irq2_counter = 0;
    ng.irq2_when = -1;
}
