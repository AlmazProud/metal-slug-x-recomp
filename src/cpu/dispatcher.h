/* Dispatch of the recompiled 68000 code.
 *
 * Each recompiled function is `void f(Cpu68k *c)`: it runs from its entry until it exits
 * (jump out of the function, return, exception, checkpoint), leaving c->pc at the next
 * address. The dispatcher looks up the function for c->pc; if there is no recompiled code for
 * that address (RAM, P2 window, code not seen during analysis) it runs one instruction with
 * the fallback interpreter (Musashi).
 *
 * JSR/BSR are translated to native calls (rc_call) that check the 68000 stack on return:
 * if the code manipulates the stack or the return address, the call ends "unclean" and
 * dispatch continues from the outer loop with the 68000 state intact.
 */
#ifndef DISPATCHER_H
#define DISPATCHER_H

#include "cpu68k.h"

typedef void (*rc_fn)(Cpu68k *c);

/* Registration of recompiled functions (called by the generated code at startup). */
void rc_register(uint32_t addr, rc_fn f);
rc_fn rc_lookup(uint32_t pc);

/* Cycle limit: when it is reached, the dispatcher calls rc_event (at loop checkpoints and
 * between dispatches). The host uses it for interrupts, video and end of execution. */
extern int64_t rc_cycle_limit;
extern volatile int rc_exit;          /* 1 = unwind everything and return to the outermost caller */
void rc_event(Cpu68k *c);             /* implemented by the host */
/* Level of the highest pending interrupt (0 = none). If it exceeds the SR mask, the dispatcher
 * calls rc_event even if the cycle limit has not been reached: this way an instruction that
 * lowers the mask (MOVE/ANDI to SR, RTE) lets the interrupt in. */
extern volatile int rc_irq_level;

/* Runs until rc_exit is set. */
void rc_run(Cpu68k *c);

/* One dispatch step: a whole recompiled function or one interpreted instruction. */
void rc_step(Cpu68k *c);

/* Native call: c->a[7] already holds the pushed return address. Returns 1 if the target
 * returned normally to `ret_addr` with the stack in place; 0 if control must go back to the
 * dispatcher. */
int rc_call(Cpu68k *c, uint32_t target, uint32_t ret_addr);

/* Fallback interpreter (musashi_interpreter.c). */
void rc_interpreter_init(void);
void rc_interpreter_step(Cpu68k *c);

/* Optional hook, called at the start of each dispatch step (lockstep tests). */
extern void (*rc_trace)(const Cpu68k *c);

/* Diagnostics: 1 = run everything with the interpreter (one step = one instruction), for
 * instruction-by-instruction traces. The cycles are the same as with the recompiled code. */
extern int rc_interpreter_only;

/* Turbo (Phase 5): each instruction costs 2^cpu_turbo times fewer cycles (cycles68k.h, CIC). */
extern int cpu_turbo;

/* Statistics */
extern uint64_t rc_recompiled_steps, rc_interpreted_steps;

#endif
