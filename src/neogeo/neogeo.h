/* Runtime de hardware de la Neo Geo MVS para el port de Metal Slug X.
 *
 * Modela lo que rodea al 68000 recompilado: mapa de memoria, protección del cartucho, LSPC
 * (VRAM, sprites, capa fix, paletas, temporización de líneas e interrupciones), entradas, reloj
 * uPD4990A, watchdog y el latch de sonido. Toda la temporización se expresa en ciclos del 68000
 * (12 MHz): 768 ciclos por línea, 264 líneas por cuadro.
 *
 * Referencia del hardware: MAME 0.289 (src/mame/snk/neogeo*.cpp, src/devices/machine/
 * upd1990a.cpp), usado SOLO como documentación. Los valores de los puertos se fijaron además
 * contra el oráculo con herramientas/mame/traza_puertos.lua.
 */
#ifndef NEOGEO_H
#define NEOGEO_H

#include <stdint.h>
#include <stdio.h>

#include "cpu68k.h"

#define NG_ANCHO 320
#define NG_ALTO 224
#define NG_CICLOS_LINEA 768
#define NG_LINEAS 264
#define NG_CICLOS_CUADRO (NG_CICLOS_LINEA * NG_LINEAS)
#define NG_PRIMERA_LINEA_VISIBLE 16          /* vpos 16..239 */
#define NG_LINEA_VBLANK 240

/* Entradas (activo en alto aquí; el bus las invierte). */
enum {
    NG_ARRIBA = 1 << 0, NG_ABAJO = 1 << 1, NG_IZQUIERDA = 1 << 2, NG_DERECHA = 1 << 3,
    NG_A = 1 << 4, NG_B = 1 << 5, NG_C = 1 << 6, NG_D = 1 << 7,
};
enum { NG_START = 1 << 0, NG_SELECT = 1 << 1, NG_MONEDA = 1 << 2 };

/* ------------------------------------------------------------------ ROMs */
typedef struct {
    uint8_t *p;              /* P1+P2 en vista del 68000 (big-endian), 5 MB */
    uint32_t p_tam;
    uint8_t *bios;           /* sp-u2 en vista del 68000, 128 KB */
    uint8_t *fix_juego;      /* formato DotEmu: 4 bpp por filas, 32 bytes por tile */
    uint8_t *fix_bios;
    uint8_t *tiles;          /* formato DotEmu: 4 bpp por filas, 128 bytes por tile */
    uint32_t n_tiles;
    uint8_t *zoom;           /* tabla L0 de reducción vertical (64 KB útiles) */
    uint8_t *z80;            /* M1: programa de sonido, 128 KB */
    uint8_t *adpcm;          /* V1-V3: muestras ADPCM, 10 MB */
    uint32_t adpcm_tam;
} NgRoms;

/* Carga y valida (SHA-256) los archivos de la copia del usuario. Devuelve 0 si todo está bien;
 * si no, escribe el motivo en `error`. */
int ng_roms_cargar(NgRoms *r, const char *carpeta, char *error, int tam_error);
void ng_roms_liberar(NgRoms *r);

/* ------------------------------------------------------------------ máquina */
typedef struct {
    /* memoria */
    uint8_t ram[0x10000];
    uint8_t sram[0x10000];
    uint16_t paleta[0x2000];             /* dos bancos de 4096 colores */
    uint32_t pen[2][0x2000];             /* [sombra][índice] -> ARGB8888 */
    uint16_t vram[0x8800];
    /* latch del sistema (74HC259) */
    uint8_t sombra, vectores_cartucho, fix_cartucho, sram_desbloqueada, banco_paleta;
    /* cartucho */
    int banco_p2;
    uint16_t prot_cmd, prot_cnt;
    /* LSPC */
    uint16_t vram_dir, vram_buffer, vram_mod;
    uint8_t anim_velocidad, anim_desactivada, anim_contador, anim_cuadros;
    uint16_t irq2_control;
    uint32_t irq2_contador;
    int64_t irq2_cuando;                 /* -1 = sin programar */
    uint8_t irq_vblank, irq_raster, irq3;
    /* E/S */
    uint8_t joy[2], sistema[2], dsw;     /* sistema: NG_START | NG_SELECT | NG_MONEDA */
    uint8_t servicio, test;
    /* Entradas crudas (bytes tal como los ve el bus, activos en bajo) para reproducir grabaciones
       de MAME: si crudo != 0 reemplazan a joy/sistema/test. */
    uint8_t crudo, crudo_p1, crudo_p2, crudo_sistema, crudo_monedas, crudo_test;
    int64_t watchdog;                    /* ciclo en que vence */
    uint32_t reinicios_watchdog;
    /* reloj del cuadro */
    int64_t inicio_cuadro;               /* ciclo de vpos 0 del cuadro actual */
    int linea;                           /* próxima línea cuyo comienzo hay que procesar */
    uint64_t cuadros;
    /* salida */
    uint32_t pantalla[NG_ALTO][NG_ANCHO];
    /* diagnóstico */
    uint64_t lecturas_sin_mapear, escrituras_sin_mapear;
} NgMaquina;

extern NgMaquina ng;
extern FILE *ng_traza;                 /* traza de diagnóstico opcional (NULL = no) */
extern FILE *ng_traza_es;              /* escrituras de E/S entre dos ciclos (comparación con MAME) */
extern int64_t ng_traza_desde, ng_traza_hasta;
extern FILE *ng_traza_lecturas;          /* lecturas de los puertos de entrada (diagnóstico) */
/* Gancho opcional: se llama cuando el 68000 reconoce la VBlank (3C000C, bit 2). */
extern void (*ng_al_reconocer_vblank)(void);
/* Gancho opcional: se llama antes de cada lectura de un puerto de entrada (reproducciones). */
extern void (*ng_antes_de_leer_entradas)(void);
extern NgRoms ng_roms;
extern uint32_t ng_pc_vblank;          /* PC interrumpido por la última VBlank (diagnóstico) */
extern Cpu68k ng_cpu;

/* Fecha inicial del reloj (si no se llama, se usa la hora local). */
void ng_rtc_fijar(int anio, int mes, int dia, int dia_semana, int hora, int minuto, int segundo);

void ng_encender(void);                  /* estado de encendido: reset del 68000 */
void ng_ejecutar_cuadro(void);
/* Próximo límite de tramo (evento de hardware), en ciclos del 68000. */
int64_t ng_proximo_limite(void);
/* Procesa los eventos de hardware vencidos hasta t (sin entregar interrupciones), para ordenar
 * una escritura de video a mitad de instrucción respecto del dibujo de las líneas. */
void ng_al_dia(int64_t t);           /* hasta el comienzo del cuadro siguiente */

/* ------------------------------------------------------------------ modo debug (Fase 6A) */
/* Mediciones que no forman parte del estado de la máquina (no cambian la emulación). */
typedef struct { int16_t x0, x1, y0, y1; } NgCaja;      /* x1 < x0: sprite no dibujado */
typedef struct {
    int activo;                          /* 1: medir carga y cajas de sprites */
    uint32_t espera_ini, espera_fin;     /* PCs del bucle de espera de la VBlank del juego */
    uint8_t ocultar;                     /* NG_OCULTAR_*: capas que no se dibujan */
    int lineas_espera;                   /* del último cuadro: inicios de línea con el 68000 esperando */
    int lineas_espera_actual;
    NgCaja caja[381];                    /* del último cuadro, en píxeles visibles (0-319, 0-223) */
    NgCaja caja_actual[381];
} NgDebug;
enum { NG_OCULTAR_SPRITES = 1, NG_OCULTAR_FIX = 2 };
extern NgDebug ng_dbg;

/* ------------------------------------------------------------------ estados guardados */
/* Estado completo de la máquina, tomado entre cuadros (después de ng_ejecutar_cuadro): 68000,
 * hardware de video y E/S, reloj, Z80 y YM2610. ng_estado_guardar devuelve un búfer de malloc;
 * ng_estado_cargar devuelve 0 si el estado es válido para esta versión del port (si no, no toca
 * la máquina). */
uint8_t *ng_estado_guardar(size_t *tam);
int ng_estado_cargar(const uint8_t *datos, size_t tam);

typedef struct {
    uint8_t *datos;
    size_t tam, pos;
    int guardando;
} NgEstado;
void ng_estado_bloque(NgEstado *e, void *p, size_t n);   /* guarda o restaura n bytes */
void ng_maquina_estado(NgEstado *e);
void ng_sonido_estado(NgEstado *e);
void ng_rtc_estado(NgEstado *e);

/* ------------------------------------------------------------------ internos */
void ng_video_iniciar(void);
void ng_video_inicio_linea(int linea);   /* dibuja la anterior y prepara la lista de sprites */
/* Solo los sprites [desde, hasta], sobre fondo transparente (0): imagen de NG_ANCHO x NG_ALTO. */
void ng_video_capturar_sprites(uint32_t *destino, int desde, int hasta);
void ng_video_anim_tick(void);
uint16_t ng_video_leer(uint32_t reg);
void ng_video_escribir(uint32_t reg, uint16_t v);
void ng_paleta_escribir(uint32_t indice, uint16_t v);
void ng_irq_actualizar(void);

void ng_rtc_iniciar(void);
void ng_rtc_escribir(uint8_t dato);      /* bits: 0 DATA IN, 1 CLK, 2 STB */
int ng_rtc_tp(void);
int ng_rtc_dato(void);

void ng_sonido_iniciar(void);
void ng_sonido_hasta(int64_t ciclos_68k);          /* avanza Z80 y YM2610 hasta ese ciclo */
int64_t ng_sonido_proximo_evento(void);            /* próximo temporizador del YM (ciclo del 68000) */
void ng_sonido_comando(uint8_t v);
uint8_t ng_sonido_respuesta(void);

/* Fase 6B: gancho opcional, llamado con cada comando de sonido del 68000 (vibración). */
extern void (*ng_al_comando_sonido)(uint8_t comando);
/* Notas iniciadas por tipo de canal del YM2610 desde el encendido (clasificar sonidos; no son
 * parte del estado de la máquina). */
typedef struct { uint32_t fm, ssg, adpcm_a, adpcm_b; } NgNotas;
extern NgNotas ng_notas;
/* Solo sonido (menú de música): el 68000 queda detenido y el Z80 y el YM2610 avanzan solos. La
 * máquina queda en un estado que no es el del juego: guardarlo antes y restaurarlo después. */
void ng_sonido_solo_comando(uint8_t comando);
void ng_sonido_solo_avanzar(int64_t ciclos_68k);

/* Audio generado (estéreo, 16 bits, 55 555 Hz). Solo se guarda si ng_audio_activo != 0. */
extern int ng_audio_activo;
#define NG_AUDIO_HZ 55555
uint32_t ng_audio_leer(int16_t *destino, uint32_t max_muestras);

void ng_bus_iniciar(void);

#endif
