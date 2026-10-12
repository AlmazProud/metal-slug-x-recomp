/* Reloj-calendario NEC uPD4990A en modo serie (C0-C2 = 1, CS = OE = 1 fijos en la MVS).
 *
 * El 68000 lo maneja con tres líneas (DATA IN, CLK, STB en 380050/51) y lee TP y DATA OUT en los
 * bits 6 y 7 de 320001. El BIOS espera flancos de TP durante el arranque (C11D6A), así que el
 * temporizado de TP importa. Los temporizadores se evalúan en función del ciclo del 68000:
 * un "toggle" periódico que empieza en t0 cambia el nivel en t0, t0 + T, t0 + 2T, ...
 *
 * Reloj del cristal: 32 768 Hz. Comandos (registro de comando de 4 bits):
 *   0 retener registro (DATA OUT = pulso de 1 Hz, TP = 64 Hz)   1 desplazar
 *   2 poner hora                                               3 leer hora
 *   4-7 TP = 64/256/2048/4096 Hz                               8-11 TP = intervalos de 1/10/30/60 s
 */
#include "neogeo.h"

#include <string.h>
#include <time.h>

#define CICLOS_SEGUNDO 12000000LL

typedef struct {
    int64_t t0, periodo;         /* periodo <= 0: detenido */
    int base;                    /* nivel antes del primer cambio */
} Pulso;

static struct {
    uint8_t shift[7];            /* 6 bytes de hora (BCD) + registro de comando */
    uint8_t hora[6];             /* segundos, minutos, horas, día, mes<<4|día de semana, año */
    uint8_t comando, clk, stb, dato_in, dato_out_fijo;
    Pulso tp, dato_out;
    int dato_out_pulso;          /* 1: DATA OUT es el pulso; 0: es el bit del registro */
    int64_t segundos_desde;      /* ciclo del último avance de segundos */
    int reloj_activo;
    int fijada;
} rtc;

static int pulso_nivel(const Pulso *p, int64_t ahora)
{
    if (p->periodo <= 0 || ahora < p->t0) return p->base;
    int64_t cambios = 1 + (ahora - p->t0) / p->periodo;
    return p->base ^ (int)(cambios & 1);
}

static void pulso_iniciar(Pulso *p, int64_t ahora, int64_t periodo)
{
    p->base = pulso_nivel(p, ahora);
    p->t0 = ahora;
    p->periodo = periodo;
}

static void pulso_detener(Pulso *p, int64_t ahora)
{
    p->base = pulso_nivel(p, ahora);
    p->periodo = 0;
}

static uint8_t bcd(int v) { return (uint8_t)((v / 10) << 4 | (v % 10)); }
static int desde_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0xF); }

static int dias_del_mes(int mes, int anio)
{
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mes == 2 && anio % 4 == 0) return 29;
    return (mes >= 1 && mes <= 12) ? d[mes - 1] : 31;
}

static void avanzar_segundo(void)
{
    int s = desde_bcd(rtc.hora[0]) + 1;
    if (s < 60) { rtc.hora[0] = bcd(s); return; }
    rtc.hora[0] = 0;
    int m = desde_bcd(rtc.hora[1]) + 1;
    if (m < 60) { rtc.hora[1] = bcd(m); return; }
    rtc.hora[1] = 0;
    int h = desde_bcd(rtc.hora[2]) + 1;
    if (h < 24) { rtc.hora[2] = bcd(h); return; }
    rtc.hora[2] = 0;
    int mes = rtc.hora[4] >> 4, dsem = rtc.hora[4] & 0xF, anio = desde_bcd(rtc.hora[5]);
    dsem = (dsem + 1) % 7;
    int d = desde_bcd(rtc.hora[3]) + 1;
    if (d > dias_del_mes(mes, anio)) {
        d = 1;
        if (++mes > 12) { mes = 1; anio = (anio + 1) % 100; }
    }
    rtc.hora[3] = bcd(d);
    rtc.hora[4] = (uint8_t)(mes << 4 | dsem);
    rtc.hora[5] = bcd(anio);
}

/* Avanza los segundos transcurridos hasta `ahora` (el divisor corre desde el encendido). */
static void actualizar(int64_t ahora)
{
    while (ahora - rtc.segundos_desde >= CICLOS_SEGUNDO) {
        rtc.segundos_desde += CICLOS_SEGUNDO;
        if (rtc.reloj_activo) avanzar_segundo();
    }
}

void ng_rtc_fijar(int anio, int mes, int dia, int dia_semana, int hora, int minuto, int segundo)
{
    rtc.hora[0] = bcd(segundo);
    rtc.hora[1] = bcd(minuto);
    rtc.hora[2] = bcd(hora);
    rtc.hora[3] = bcd(dia);
    rtc.hora[4] = (uint8_t)(mes << 4 | dia_semana);
    rtc.hora[5] = bcd(anio % 100);
    rtc.fijada = 1;
}

void ng_rtc_iniciar(void)
{
    int fijada = rtc.fijada;
    uint8_t hora[6];
    for (int i = 0; i < 6; i++) hora[i] = rtc.hora[i];
    memset(&rtc, 0, sizeof rtc);
    if (fijada) {
        for (int i = 0; i < 6; i++) rtc.hora[i] = hora[i];
        rtc.fijada = 1;
    } else {
        time_t t = time(NULL);
        struct tm *l = localtime(&t);
        ng_rtc_fijar(l->tm_year, l->tm_mon + 1, l->tm_mday, l->tm_wday, l->tm_hour, l->tm_min, l->tm_sec);
        rtc.fijada = 0;
    }
    rtc.segundos_desde = ng_cpu.ciclos;
    rtc.reloj_activo = 1;
}

static void comando(int64_t ahora)
{
    rtc.comando = rtc.shift[6];
    int c = rtc.comando;
    if (c == 0 || (c >= 4 && c < 15)) rtc.reloj_activo = 1;
    switch (c) {
    case 0:
        rtc.dato_out_pulso = 1;
        pulso_iniciar(&rtc.dato_out, ahora, CICLOS_SEGUNDO / 2);
        pulso_iniciar(&rtc.tp, ahora, CICLOS_SEGUNDO / 128);
        break;
    case 1:
        pulso_detener(&rtc.dato_out, ahora);
        rtc.dato_out_pulso = 0;
        break;
    case 2:
        rtc.reloj_activo = 0;
        pulso_detener(&rtc.dato_out, ahora);
        rtc.dato_out_pulso = 0;
        for (int i = 0; i < 6; i++) rtc.hora[i] = rtc.shift[i];
        break;
    case 3:
        rtc.reloj_activo = 1;
        for (int i = 0; i < 6; i++) rtc.shift[i] = rtc.hora[i];
        rtc.dato_out_pulso = 1;
        pulso_iniciar(&rtc.dato_out, ahora, CICLOS_SEGUNDO / 2);
        break;
    case 4: case 5: case 6: case 7: {
        static const int64_t hz[4] = {64, 256, 2048, 4096};
        pulso_iniciar(&rtc.tp, ahora, CICLOS_SEGUNDO / (hz[c - 4] * 2));
        break;
    }
    case 8: case 9: case 10: case 11: {
        static const int64_t seg[4] = {1, 10, 30, 60};
        pulso_iniciar(&rtc.tp, ahora, CICLOS_SEGUNDO * seg[c - 8] / 2);
        break;
    }
    default: break;               /* 12-14 interrupciones, 15 prueba: no se usan */
    }
}

void ng_rtc_escribir(uint8_t dato)
{
    int64_t ahora = ng_cpu.ciclos;
    actualizar(ahora);
    int din = dato & 1, clk = (dato >> 1) & 1, stb = (dato >> 2) & 1;
    rtc.dato_in = (uint8_t)din;
    if (!rtc.clk && clk) {                       /* flanco de subida de CLK */
        int entra = rtc.shift[6] & 1;            /* el registro de comando desborda al de hora */
        rtc.shift[6] = (uint8_t)((rtc.shift[6] >> 1) | (din << 3));
        if (rtc.comando == 1) {
            for (int i = 0; i < 6; i++) {
                rtc.shift[i] >>= 1;
                if (i == 5) rtc.shift[i] |= (uint8_t)(entra << 7);
                else rtc.shift[i] |= (uint8_t)((rtc.shift[i + 1] << 7) & 0x80);
            }
        }
    }
    rtc.clk = (uint8_t)clk;
    if (!rtc.stb && stb) comando(ahora);         /* flanco de subida de STB */
    rtc.stb = (uint8_t)stb;
}

int ng_rtc_tp(void)
{
    return pulso_nivel(&rtc.tp, ng_cpu.ciclos);
}

int ng_rtc_dato(void)
{
    actualizar(ng_cpu.ciclos);
    if (rtc.dato_out_pulso) return pulso_nivel(&rtc.dato_out, ng_cpu.ciclos);
    return rtc.shift[0] & 1;
}
