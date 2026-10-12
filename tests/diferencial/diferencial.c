/* Pruebas diferenciales del recompilador: cada instrucción real del juego se ejecuta desde
 * estados aleatorios en Musashi (referencia) y en el C generado, y se comparan registros, SR,
 * PC y escrituras en memoria.
 *
 * Uso: diferencial <mslugx_game_m68k> <mslugx_bios_m68k> [semillas por instrucción]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu68k.h"
#include "estado.h"
#include "memoria_prueba.h"
#include "pruebas.h"

void ms_iniciar(void);
int ms_ejecutar(Estado *e);

static uint32_t rng;
static uint32_t azar(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void estado_aleatorio(Estado *e, uint32_t pc)
{
    for (int i = 0; i < 8; i++) { e->d[i] = azar(); e->a[i] = azar(); }
    /* Valores frecuentes en el juego: pequeños, cero y bordes, para ejercitar flags. */
    if ((azar() & 3) == 0) e->d[azar() & 7] = 0;
    if ((azar() & 3) == 0) e->d[azar() & 7] = azar() & 0xFF;
    if ((azar() & 3) == 0) e->d[azar() & 7] = 0x80000000u >> (azar() & 31);
    e->sr = (azar() & 0x1F) | (azar() & 1) << 13 | (azar() & 7) << 8;
    e->usp = azar();
    e->ssp = azar();
    e->a[7] = (e->sr & 0x2000) ? e->ssp : e->usp;
    e->pc = pc;
}

static void ejecutar_recompilado(const Prueba *p, Estado *e)
{
    Cpu68k c;
    memset(&c, 0, sizeof c);
    memcpy(c.d, e->d, sizeof c.d);
    memcpy(c.a, e->a, sizeof c.a);
    c.s = (e->sr >> 13) & 1;
    c.t = (e->sr >> 15) & 1;
    c.imask = (e->sr >> 8) & 7;
    cpu_set_ccr(&c, e->sr);
    c.usp = e->usp;
    c.ssp = e->ssp;
    c.a[7] = c.s ? e->ssp : e->usp;
    c.pc = e->pc;
    p->fn(&c);
    memcpy(e->d, c.d, sizeof c.d);
    memcpy(e->a, c.a, sizeof c.a);
    e->sr = cpu_get_sr(&c);
    if (c.s) { e->ssp = c.a[7]; e->usp = c.usp; } else { e->usp = c.a[7]; e->ssp = c.ssp; }
    e->pc = c.pc;
}

static int comparar(const Estado *m, const Estado *r, const Capa *cm, const Capa *cr, char *det, size_t n)
{
    int ok = 1;
    size_t k = 0;
#define DIF(...) do { ok = 0; if (k < n) k += (size_t)snprintf(det + k, n - k, __VA_ARGS__); } while (0)
    for (int i = 0; i < 8; i++) {
        if (m->d[i] != r->d[i]) DIF(" d%d musashi=%08X recomp=%08X;", i, m->d[i], r->d[i]);
        if (m->a[i] != r->a[i]) DIF(" a%d musashi=%08X recomp=%08X;", i, m->a[i], r->a[i]);
    }
    if (m->usp != r->usp) DIF(" usp musashi=%08X recomp=%08X;", m->usp, r->usp);
    if (m->ssp != r->ssp) DIF(" ssp musashi=%08X recomp=%08X;", m->ssp, r->ssp);
    if ((m->sr & 0xA71F) != (r->sr & 0xA71F)) DIF(" sr musashi=%04X recomp=%04X;", m->sr & 0xA71F, r->sr & 0xA71F);
    if ((m->pc & 0xFFFFFF) != (r->pc & 0xFFFFFF)) DIF(" pc musashi=%06X recomp=%06X;", m->pc & 0xFFFFFF, r->pc & 0xFFFFFF);
    const Capa *capas[2] = {cm, cr};
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < capas[c]->n; i++) {
            uint32_t a = capas[c]->dir[i];
            uint8_t vm = mem_leer8_en(cm, a), vr = mem_leer8_en(cr, a);
            if (vm != vr) { DIF(" mem[%06X] musashi=%02X recomp=%02X;", a, vm, vr); break; }
        }
#undef DIF
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: %s <mslugx_game_m68k> <mslugx_bios_m68k> [semillas]\n", argv[0]);
        return 2;
    }
    int semillas = argc > 3 ? atoi(argv[3]) : 64;
    int depurar = getenv("PD_DEPURAR") != NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    mem_cargar_roms(argv[1], argv[2]);
    static Capa capa_m, capa_r;
    mem_usar_capa(&capa_m);          /* el reset de Musashi lee los vectores */
    ms_iniciar();
    int fallidas = 0, casos = 0;
    for (int t = 0; t < n_pruebas; t++) {
        const Prueba *p = &pruebas[t];
        int fallos = 0;
        if (depurar) fprintf(stderr, "%d %06X %s\n", t, p->dir, p->texto);
        char primero[512] = "";
        for (int s = 0; s < semillas; s++) {
            rng = 0x12345678u ^ (uint32_t)(t * 7919 + s * 104729 + 1);
            Estado ini, em, er;
            estado_aleatorio(&ini, p->dir);
            mem_semilla(azar());
            em = ini; er = ini;
            capa_m.n = 0; mem_usar_capa(&capa_m); ms_ejecutar(&em);
            capa_r.n = 0; mem_usar_capa(&capa_r); ejecutar_recompilado(p, &er);
            casos++;
            char det[400];
            if (!comparar(&em, &er, &capa_m, &capa_r, det, sizeof det)) {
                if (!fallos)
                    snprintf(primero, sizeof primero, "semilla %d, sr_ini=%04X:%s", s, ini.sr, det);
                fallos++;
            }
        }
        if (fallos) {
            fallidas++;
            printf("FALLA %06X  %-34s  %d/%d  %s\n", p->dir, p->texto, fallos, semillas, primero);
        }
    }
    printf("\nInstrucciones probadas: %d  casos: %d  instrucciones con diferencias: %d\n",
           n_pruebas, casos, fallidas);
    return fallidas ? 1 : 0;
}
