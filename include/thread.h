#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>

#include "list.h"

#define THREAD_STACK_SIZE 4096UL

enum thread_state {
    THREAD_RUNNING = 0,
    THREAD_ZOMBIE = 1,
};

struct cpu_context {
    uint64_t ra;
    uint64_t sp;
    uint64_t s0;
    uint64_t s1;
    uint64_t s2;
    uint64_t s3;
    uint64_t s4;
    uint64_t s5;
    uint64_t s6;
    uint64_t s7;
    uint64_t s8;
    uint64_t s9;
    uint64_t s10;
    uint64_t s11;
};

struct trap_frame {
    uint64_t x[35];
};

struct thread {
    struct cpu_context context;
    int pid;
    int state;
    void *kernel_stack;
    void (*entry)(void);
    struct list_head list;
};

void thread_system_init(void);
struct thread *thread_create(void (*func)(void));
void schedule(void);
void thread_exit(void);
void kill_zombies(void);
void idle(void);

static inline struct thread *get_current(void) {
    struct thread *t;
    asm volatile("mv %0, tp" : "=r"(t));
    return t;
}

void switch_to(struct thread *prev, struct thread *next);

#endif /* THREAD_H */
