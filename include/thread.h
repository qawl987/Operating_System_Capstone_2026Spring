#ifndef THREAD_H
#define THREAD_H

#include <stdint.h>

#include "list.h"

#define THREAD_STACK_SIZE 4096UL
#define USER_STACK_SIZE 4096UL
#define SIGNAL_MAX 32

enum thread_state {
    THREAD_RUNNING = 0,
    THREAD_ZOMBIE = 1,
    THREAD_SLEEPING = 2,
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
    int exit_code;
    uint64_t wake_time;
    void (*signal_handlers[SIGNAL_MAX])(void);
    uint32_t pending_signals;
    struct trap_frame backup_trap_frame;
    void *signal_stack;
    int processing_signal;
    void *kernel_stack;
    void *user_stack;
    void (*entry)(void);
    struct thread *parent;
    struct list_head list;
    struct list_head all_list;
};

void thread_system_init(void);
struct thread *thread_create(void (*func)(void));
void schedule(void);
void thread_exit(void);
void process_exit(int status);
long process_waitpid(long pid);
int process_stop(long pid);
long process_usleep(unsigned int usec);
long process_signal(int signum, void (*handler)(void));
void process_sigreturn(struct trap_frame *regs);
long process_kill(int pid, int signum);
void check_pending_signals(struct trap_frame *regs);
long process_fork(struct trap_frame *regs);
int process_exec_image(const void *image, unsigned long size);
int process_spawn_user(const void *image, unsigned long size);
struct thread *thread_find(int pid);
void thread_wake_sleepers(uint64_t now);
void kill_zombies(void);
void idle(void);

static inline struct thread *get_current(void) {
    struct thread *t;
    asm volatile("mv %0, tp" : "=r"(t));
    return t;
}

void switch_to(struct thread *prev, struct thread *next);

#endif /* THREAD_H */
