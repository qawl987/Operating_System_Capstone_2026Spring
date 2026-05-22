#ifndef TRAP_H
#define TRAP_H

#include <stddef.h>
#include <stdint.h>

struct pt_regs {
    unsigned long ra;
    unsigned long sp;
    unsigned long gp;
    unsigned long tp;
    unsigned long t0;
    unsigned long t1;
    unsigned long t2;
    unsigned long s0;
    unsigned long s1;
    unsigned long a0;
    unsigned long a1;
    unsigned long a2;
    unsigned long a3;
    unsigned long a4;
    unsigned long a5;
    unsigned long a6;
    unsigned long a7;
    unsigned long s2;
    unsigned long s3;
    unsigned long s4;
    unsigned long s5;
    unsigned long s6;
    unsigned long s7;
    unsigned long s8;
    unsigned long s9;
    unsigned long s10;
    unsigned long s11;
    unsigned long t3;
    unsigned long t4;
    unsigned long t5;
    unsigned long t6;
    unsigned long epc;
    unsigned long status;
    unsigned long cause;
    unsigned long badvaddr;
};

typedef void (*timer_callback_t)(void *arg);
typedef void (*task_callback_t)(void *arg);

void trap_init(uint64_t hart_id, uint64_t timer_tick_hz);
int trap_exec_user(const void *entry, size_t size);
void do_trap(struct pt_regs *regs);
void trap_enter_loader_mode(void);
int add_timer(timer_callback_t callback, void *arg, int sec);
int add_task(task_callback_t callback, void *arg, int priority);
uint64_t trap_uptime_seconds(void);

#endif /* TRAP_H */
