#include "thread.h"

#include "kmalloc.h"
#include "uart.h"

static LIST_HEAD(run_queue);
static LIST_HEAD(zombie_queue);
static struct thread *idle_thread;
static int next_pid = 1;

static struct thread *alloc_thread(void (*func)(void)) {
    struct thread *t = (struct thread *)allocate(sizeof(struct thread));
    if (t == (void *)0) {
        return (void *)0;
    }
    void *stack = allocate(THREAD_STACK_SIZE);
    if (stack == (void *)0) {
        free(t);
        return (void *)0;
    }

    for (unsigned int i = 0; i < sizeof(struct cpu_context) / sizeof(uint64_t);
         i++) {
        ((uint64_t *)&t->context)[i] = 0;
    }

    t->pid = next_pid++;
    t->state = THREAD_RUNNING;
    t->kernel_stack = stack;
    t->entry = func;
    INIT_LIST_HEAD(&t->list);

    uint64_t stack_top = (uint64_t)stack + THREAD_STACK_SIZE;
    stack_top -= sizeof(struct trap_frame);
    t->context.sp = stack_top;
    t->context.ra = (uint64_t)func;
    return t;
}

void thread_system_init(void) {
    struct thread *boot = (struct thread *)allocate(sizeof(struct thread));
    if (boot == (void *)0) {
        return;
    }
    for (unsigned int i = 0; i < sizeof(struct cpu_context) / sizeof(uint64_t);
         i++) {
        ((uint64_t *)&boot->context)[i] = 0;
    }
    boot->pid = 0;
    boot->state = THREAD_RUNNING;
    boot->kernel_stack = (void *)0;
    boot->entry = (void *)0;
    INIT_LIST_HEAD(&boot->list);
    asm volatile("mv tp, %0" : : "r"(boot));

    idle_thread = alloc_thread(idle);
    if (idle_thread != (void *)0) {
        list_add_tail(&idle_thread->list, &run_queue);
    }
}

struct thread *thread_create(void (*func)(void)) {
    if (func == (void *)0) {
        return (void *)0;
    }
    struct thread *t = alloc_thread(func);
    if (t == (void *)0) {
        return (void *)0;
    }
    list_add_tail(&t->list, &run_queue);
    return t;
}

void schedule(void) {
    struct thread *prev = get_current();
    struct thread *next = (void *)0;

    if (prev != (void *)0 && prev->state == THREAD_RUNNING &&
        prev != idle_thread) {
        list_add_tail(&prev->list, &run_queue);
    } else if (prev != (void *)0 && prev->state == THREAD_ZOMBIE) {
        list_add_tail(&prev->list, &zombie_queue);
    }

    if (!list_empty(&run_queue)) {
        next = list_first_entry(&run_queue, struct thread, list);
        list_del_init(&next->list);
    } else if (idle_thread != (void *)0 && idle_thread != prev &&
               idle_thread->state == THREAD_RUNNING) {
        next = idle_thread;
    } else {
        next = prev;
    }

    if (next == prev || next == (void *)0 || prev == (void *)0) {
        return;
    }
    switch_to(prev, next);
}

void thread_exit(void) {
    struct thread *cur = get_current();
    if (cur == (void *)0) {
        return;
    }
    cur->state = THREAD_ZOMBIE;
    schedule();
    while (1) {
        asm volatile("wfi");
    }
}

void kill_zombies(void) {
    struct list_head *pos = (void *)0;
    struct list_head *n = (void *)0;
    list_for_each_safe(pos, n, &zombie_queue) {
        struct thread *t = list_entry(pos, struct thread, list);
        list_del_init(&t->list);
        if (t == idle_thread) {
            continue;
        }
        if (t->kernel_stack != (void *)0) {
            free(t->kernel_stack);
        }
        free(t);
    }
}

void idle(void) {
    while (1) {
        kill_zombies();
        schedule();
    }
}
