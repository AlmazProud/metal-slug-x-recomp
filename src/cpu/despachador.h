/* Despacho del código recompilado del 68000.
 *
 * Cada función recompilada es `void f(Cpu68k *c)`: ejecuta desde su entrada hasta que sale
 * (salto fuera de la función, retorno, excepción, punto de control) dejando c->pc en la
 * siguiente dirección. El despachador busca la función de c->pc; si no hay código recompilado
 * para esa dirección (RAM, ventana de P2, código no visto en el análisis) ejecuta una
 * instrucción con el intérprete de respaldo (Musashi).
 *
 * JSR/BSR se traducen a llamadas nativas (rc_llamar) que verifican la pila del 68000 al volver:
 * si el código manipula la pila o la dirección de retorno, la llamada termina "no limpia" y el
 * despacho continúa desde el bucle superior con el estado del 68000 intacto.
 */
#ifndef DESPACHADOR_H
#define DESPACHADOR_H

#include "cpu68k.h"

typedef void (*rc_fn)(Cpu68k *c);

/* Registro de funciones recompiladas (lo llama el código generado al iniciar). */
void rc_registrar(uint32_t dir, rc_fn f);
rc_fn rc_buscar(uint32_t pc);

/* Límite de ciclos: al alcanzarlo, el despachador llama a rc_evento (en los puntos de control
 * de los bucles y entre despachos). El anfitrión lo usa para interrupciones, video y fin de
 * ejecución. */
extern int64_t rc_limite_ciclos;
extern volatile int rc_salir;          /* 1 = desenrollar todo y volver al llamador superior */
void rc_evento(Cpu68k *c);             /* lo implementa el anfitrión */
/* Nivel de la interrupción pendiente más alta (0 = ninguna). Si supera la máscara del SR, el
 * despachador llama a rc_evento aunque no se haya alcanzado el límite de ciclos: así una
 * instrucción que baja la máscara (MOVE/ANDI a SR, RTE) deja entrar la interrupción. */
extern volatile int rc_irq_nivel;

/* Ejecuta hasta que rc_salir se active. */
void rc_ejecutar(Cpu68k *c);

/* Un paso de despacho: una función recompilada completa o una instrucción interpretada. */
void rc_paso(Cpu68k *c);

/* Llamada nativa: c->a[7] ya tiene la dirección de retorno apilada. Devuelve 1 si el destino
 * volvió normalmente a `retorno` con la pila en su sitio; 0 si hay que volver al despachador. */
int rc_llamar(Cpu68k *c, uint32_t destino, uint32_t retorno);

/* Intérprete de respaldo (interprete_musashi.c). */
void rc_interprete_iniciar(void);
void rc_interprete_paso(Cpu68k *c);

/* Gancho opcional, llamado al inicio de cada paso de despacho (pruebas de lockstep). */
extern void (*rc_traza)(const Cpu68k *c);

/* Diagnóstico: 1 = ejecutar todo con el intérprete (un paso = una instrucción), para trazas
 * instrucción a instrucción. Los ciclos son los mismos que los del código recompilado. */
extern int rc_solo_interprete;

/* Turbo (Fase 5): cada instrucción cuesta 2^cpu_turbo veces menos ciclos (ciclos68k.h, CIC). */
extern int cpu_turbo;

/* Estadísticas */
extern uint64_t rc_pasos_recompilados, rc_pasos_interpretados;

#endif
