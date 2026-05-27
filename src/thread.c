#include "thread.h"

#include "helper.h"
#include "kmalloc.h"
#include "process.h"
#include "vm.h"

static LIST_HEAD(run_queue);
static LIST_HEAD(zombie_queue);
static LIST_HEAD(all_threads);
static struct thread *idle_thread;
static int next_pid = 1;

static inline unsigned long irq_save(void) {
    unsigned long s;
    asm volatile("csrr %0, sstatus" : "=r"(s));
    asm volatile("csrci sstatus, 2" ::: "memory");
    return s;
}

static inline void irq_restore(unsigned long s) {
    if (s & 2UL) {
        asm volatile("csrsi sstatus, 2" ::: "memory");
    } else {
        asm volatile("csrci sstatus, 2" ::: "memory");
    }
}

int thread_alloc_pid(void) {
    return next_pid++;
}

int thread_is_idle(struct thread *t) {
    return t == idle_thread;
}

uint64_t thread_rdtime(void) {
    uint64_t t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

void thread_add_to_all(struct thread *t) {
    if (t == (void *)0) {
        return;
    }
    unsigned long irq_state = irq_save();
    list_add_tail(&t->all_list, &all_threads);
    irq_restore(irq_state);
}

void thread_make_runnable(struct thread *t) {
    if (t == (void *)0 || t == idle_thread) {
        return;
    }
    unsigned long irq_state = irq_save();
    if (list_empty(&t->list)) {
        list_add_tail(&t->list, &run_queue);
    }
    irq_restore(irq_state);
}

void thread_make_zombie(struct thread *t) {
    if (t == (void *)0 || t == idle_thread) {
        return;
    }
    unsigned long irq_state = irq_save();
    if (!list_empty(&t->list)) {
        list_del_init(&t->list);
    }
    list_add_tail(&t->list, &zombie_queue);
    irq_restore(irq_state);
}

struct thread *thread_find(int pid) {
    struct list_head *pos = (void *)0;
    list_for_each(pos, &all_threads) {
        struct thread *t = list_entry(pos, struct thread, all_list);
        if (t->pid == pid) {
            return t;
        }
    }
    return (void *)0;
}

struct thread *thread_find_zombie_child(long pid) {
    struct list_head *pos = (void *)0;
    list_for_each(pos, &all_threads) {
        struct thread *t = list_entry(pos, struct thread, all_list);
        if (t->parent != get_current()) {
            continue;
        }
        if (pid >= 0 && t->pid != pid) {
            continue;
        }
        if (t->state == THREAD_ZOMBIE) {
            return t;
        }
    }
    return (void *)0;
}

struct thread *thread_alloc(void (*func)(void)) {
    struct thread *t = (struct thread *)allocate(sizeof(struct thread));
    if (t == (void *)0) {
        return (void *)0;
    }
    void *stack = allocate(THREAD_STACK_SIZE);
    if (stack == (void *)0) {
        free(t);
        return (void *)0;
    }

    memset(t, 0, sizeof(*t));
    t->pid = thread_alloc_pid();
    t->state = THREAD_RUNNING;
    t->kernel_stack = stack;
    t->pgd = vm_kernel_pgd();
    t->mmap_next = USER_MMAP_BASE;
    t->entry = func;
    t->parent = get_current();
    INIT_LIST_HEAD(&t->list);
    INIT_LIST_HEAD(&t->all_list);

    uint64_t stack_top = (uint64_t)stack + THREAD_STACK_SIZE;
    stack_top -= sizeof(struct trap_frame);
    t->context.sp = stack_top;
    t->context.ra = (uint64_t)func;
    thread_add_to_all(t);
    return t;
}

void thread_free(struct thread *t) {
    if (t == (void *)0 || t == idle_thread || t == get_current()) {
        return;
    }
    unsigned long irq_state = irq_save();
    list_del_init(&t->all_list);
    if (!list_empty(&t->list)) {
        list_del_init(&t->list);
    }
    irq_restore(irq_state);
    if (t->kernel_stack != (void *)0) {
        free(t->kernel_stack);
    }
    if (t->user_stack != (void *)0) {
        free(t->user_stack);
    }
    if (t->signal_stack != (void *)0) {
        free(t->signal_stack);
    }
    free(t);
}

void thread_system_init(void) {
    struct thread *boot = (struct thread *)allocate(sizeof(struct thread));
    if (boot == (void *)0) {
        return;
    }
    memset(boot, 0, sizeof(*boot));
    boot->pid = thread_alloc_pid();
    boot->state = THREAD_RUNNING;
    boot->pgd = vm_kernel_pgd();
    boot->mmap_next = USER_MMAP_BASE;
    INIT_LIST_HEAD(&boot->list);
    INIT_LIST_HEAD(&boot->all_list);
    thread_add_to_all(boot);
    asm volatile("mv tp, %0" : : "r"(boot));

    idle_thread = thread_alloc(idle);
    if (idle_thread != (void *)0) {
        unsigned long irq_state = irq_save();
        list_add_tail(&idle_thread->list, &run_queue);
        irq_restore(irq_state);
    }
}

struct thread *thread_create(void (*func)(void)) {
    if (func == (void *)0) {
        return (void *)0;
    }
    struct thread *t = thread_alloc(func);
    if (t == (void *)0) {
        return (void *)0;
    }
    t->parent = (void *)0;
    thread_make_runnable(t);
    return t;
}

void schedule(void) {
    struct thread *prev = get_current();
    struct thread *next = (void *)0;

    unsigned long irq_state = irq_save();
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
        irq_restore(irq_state);
        return;
    }
    irq_restore(irq_state);
    vm_switch(next->pgd);
    switch_to(prev, next);
}

void thread_exit(void) {
    process_exit(0);
}

void thread_wake_sleepers(uint64_t now) {
    struct list_head *pos = (void *)0;
    unsigned long irq_state = irq_save();
    list_for_each(pos, &all_threads) {
        struct thread *t = list_entry(pos, struct thread, all_list);
        if (t->state == THREAD_SLEEPING && now >= t->wake_time) {
            t->state = THREAD_RUNNING;
            t->wake_time = 0;
            if (list_empty(&t->list)) {
                list_add_tail(&t->list, &run_queue);
            }
        }
    }
    irq_restore(irq_state);
}

void kill_zombies(void) {
    struct list_head *pos = (void *)0;
    struct list_head *n = (void *)0;
    unsigned long irq_state = irq_save();
    list_for_each_safe(pos, n, &zombie_queue) {
        struct thread *t = list_entry(pos, struct thread, list);
        list_del_init(&t->list);
        if (t == idle_thread) {
            continue;
        }
        if (t->parent != (void *)0 && t->parent->state != THREAD_ZOMBIE) {
            list_add_tail(&t->list, &zombie_queue);
            continue;
        }
        irq_restore(irq_state);
        if (t->parent != (void *)0) {
            printf("[INFO] Killing zombie thread with PID: %d\n", t->pid);
        }
        thread_free(t);
        irq_state = irq_save();
    }
    irq_restore(irq_state);
}

void idle(void) {
    while (1) {
        kill_zombies();
        schedule();
    }
}
