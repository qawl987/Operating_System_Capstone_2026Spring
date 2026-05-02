#include "thread.h"

#include "config.h"
#include "helper.h"
#include "kmalloc.h"
#include "uart.h"

extern void ret_from_exception(void);

static LIST_HEAD(run_queue);
static LIST_HEAD(zombie_queue);
static LIST_HEAD(all_threads);
static struct thread *idle_thread;
static int next_pid = 1;

#define TF_SP 1
#define TF_RA 0
#define TF_A0 9
#define TF_EPC 31
#define TF_STATUS 32
#define USER_IMAGE_BASE TEST_MEM_BASE
#define SIGNAL_TRAMPOLINE_SIZE 16UL

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

    memset(t, 0, sizeof(*t));

    t->pid = next_pid++;
    t->state = THREAD_RUNNING;
    t->exit_code = 0;
    t->wake_time = 0;
    t->kernel_stack = stack;
    t->user_stack = (void *)0;
    t->entry = func;
    t->parent = get_current();
    INIT_LIST_HEAD(&t->list);
    INIT_LIST_HEAD(&t->all_list);

    uint64_t stack_top = (uint64_t)stack + THREAD_STACK_SIZE;
    stack_top -= sizeof(struct trap_frame);
    t->context.sp = stack_top;
    t->context.ra = (uint64_t)func;
    list_add_tail(&t->all_list, &all_threads);
    return t;
}

static void free_thread(struct thread *t) {
    if (t == (void *)0 || t == idle_thread || t == get_current()) {
        return;
    }
    printf("[INFO] Killing zombie thread with PID: %d\n", t->pid);
    list_del_init(&t->all_list);
    if (!list_empty(&t->list)) {
        list_del_init(&t->list);
    }
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

static int is_child_of_current(struct thread *t) {
    return t != (void *)0 && t->parent == get_current();
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

void thread_system_init(void) {
    struct thread *boot = (struct thread *)allocate(sizeof(struct thread));
    if (boot == (void *)0) {
        return;
    }
    memset(boot, 0, sizeof(*boot));
    boot->pid = next_pid++;
    boot->state = THREAD_RUNNING;
    boot->exit_code = 0;
    boot->wake_time = 0;
    boot->kernel_stack = (void *)0;
    boot->user_stack = (void *)0;
    boot->entry = (void *)0;
    boot->parent = (void *)0;
    INIT_LIST_HEAD(&boot->list);
    INIT_LIST_HEAD(&boot->all_list);
    list_add_tail(&boot->all_list, &all_threads);
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
    t->parent = (void *)0;
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
    process_exit(0);
}

void process_exit(int status) {
    struct thread *cur = get_current();
    if (cur == (void *)0) {
        return;
    }
    cur->exit_code = status;
    cur->state = THREAD_ZOMBIE;
    schedule();
    while (1) {
        asm volatile("wfi");
    }
}

long process_waitpid(long pid) {
    while (1) {
        struct list_head *pos = (void *)0;
        list_for_each(pos, &all_threads) {
            struct thread *t = list_entry(pos, struct thread, all_list);
            if (!is_child_of_current(t)) {
                continue;
            }
            if (pid >= 0 && t->pid != pid) {
                continue;
            }
            if (t->state == THREAD_ZOMBIE) {
                long ret = t->pid;
                free_thread(t);
                return ret;
            }
        }

        if (pid >= 0) {
            struct thread *target = thread_find((int)pid);
            if (target == (void *)0 || target->parent != get_current()) {
                return -1;
            }
        }
        schedule();
    }
}

int process_stop(long pid) {
    struct thread *target = thread_find((int)pid);
    if (target == (void *)0 || target == idle_thread || target->state == THREAD_ZOMBIE) {
        return -1;
    }
    if (target == get_current()) {
        process_exit(0);
        return 0;
    }
    target->state = THREAD_ZOMBIE;
    target->parent = (void *)0;
    free_thread(target);
    return 0;
}

long process_signal(int signum, void (*handler)(void)) {
    struct thread *cur = get_current();
    if (cur == (void *)0 || signum < 0 || signum >= SIGNAL_MAX) {
        return -1;
    }
    void (*old)(void) = cur->signal_handlers[signum];
    cur->signal_handlers[signum] = handler;
    return (long)old;
}

void process_sigreturn(struct trap_frame *regs) {
    struct thread *cur = get_current();
    if (cur == (void *)0 || regs == (void *)0 || !cur->processing_signal) {
        return;
    }

    printf("[INFO] SIGRETURN is called!\n");
    void *stack = cur->signal_stack;
    memcpy(regs, &cur->backup_trap_frame, sizeof(*regs));
    cur->processing_signal = 0;
    cur->signal_stack = (void *)0;
    if (stack != (void *)0) {
        free(stack);
    }
}

long process_kill(int pid, int signum) {
    if (signum < 0 || signum >= SIGNAL_MAX) {
        return -1;
    }
    struct thread *target = thread_find(pid);
    if (target == (void *)0 || target == idle_thread ||
        target->state == THREAD_ZOMBIE) {
        return -1;
    }
    if (target->signal_handlers[signum] == (void *)0) {
        return process_stop(pid);
    }

    target->pending_signals |= (1U << signum);
    if (target->state == THREAD_SLEEPING) {
        target->state = THREAD_RUNNING;
        target->wake_time = 0;
        if (list_empty(&target->list)) {
            list_add_tail(&target->list, &run_queue);
        }
    }
    return 0;
}

void check_pending_signals(struct trap_frame *regs) {
    struct thread *cur = get_current();
    if (cur == (void *)0 || regs == (void *)0 ||
        cur->state != THREAD_RUNNING || cur->processing_signal ||
        cur->pending_signals == 0) {
        return;
    }

    int signum = -1;
    for (int i = 0; i < SIGNAL_MAX; i++) {
        if ((cur->pending_signals & (1U << i)) != 0) {
            signum = i;
            break;
        }
    }
    if (signum < 0) {
        return;
    }

    void (*handler)(void) = cur->signal_handlers[signum];
    cur->pending_signals &= ~(1U << signum);
    if (handler == (void *)0) {
        process_exit(0);
        return;
    }

    void *stack = allocate(USER_STACK_SIZE);
    if (stack == (void *)0) {
        cur->pending_signals |= (1U << signum);
        return;
    }

    memcpy(&cur->backup_trap_frame, regs, sizeof(*regs));
    uint64_t tramp = ((uint64_t)stack + USER_STACK_SIZE -
                      SIGNAL_TRAMPOLINE_SIZE) &
                     ~0xFUL;
    uint32_t *code = (uint32_t *)tramp;
    code[0] = 0x00b00893U;
    code[1] = 0x00000073U;
    asm volatile(".word 0x0000100f" ::: "memory");

    cur->signal_stack = stack;
    cur->processing_signal = 1;
    regs->x[TF_EPC] = (uint64_t)handler;
    regs->x[TF_SP] = tramp;
    regs->x[TF_RA] = tramp;
}

static uint64_t rdtime(void) {
    uint64_t t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

long process_usleep(unsigned int usec) {
    struct thread *cur = get_current();
    if (cur == (void *)0) {
        return -1;
    }
    if (usec == 0) {
        schedule();
        return 0;
    }

    uint64_t delay = ((uint64_t)usec * TIMER_TICK_HZ) / 1000000ULL;
    if (delay == 0) {
        delay = 1;
    }
    cur->wake_time = rdtime() + delay;
    cur->state = THREAD_SLEEPING;
    schedule();
    return 0;
}

void thread_wake_sleepers(uint64_t now) {
    struct list_head *pos = (void *)0;
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
}

long process_fork(struct trap_frame *regs) {
    struct thread *parent = get_current();
    if (parent == (void *)0 || parent->kernel_stack == (void *)0 ||
        regs == (void *)0) {
        return -1;
    }

    struct thread *child = (struct thread *)allocate(sizeof(struct thread));
    if (child == (void *)0) {
        return -1;
    }
    void *kstack = allocate(THREAD_STACK_SIZE);
    if (kstack == (void *)0) {
        free(child);
        return -1;
    }
    void *ustack = allocate(USER_STACK_SIZE);
    if (ustack == (void *)0) {
        free(kstack);
        free(child);
        return -1;
    }

    memcpy(child, parent, sizeof(*child));
    memcpy(kstack, parent->kernel_stack, THREAD_STACK_SIZE);
    if (parent->user_stack != (void *)0) {
        memcpy(ustack, parent->user_stack, USER_STACK_SIZE);
    } else {
        memset(ustack, 0, USER_STACK_SIZE);
    }

    child->pid = next_pid++;
    child->state = THREAD_RUNNING;
    child->exit_code = 0;
    child->wake_time = 0;
    child->pending_signals = 0;
    child->processing_signal = 0;
    child->signal_stack = (void *)0;
    child->kernel_stack = kstack;
    child->user_stack = ustack;
    child->parent = parent;
    INIT_LIST_HEAD(&child->list);
    INIT_LIST_HEAD(&child->all_list);

    uint64_t tf_off = (uint64_t)regs - (uint64_t)parent->kernel_stack;
    struct trap_frame *child_regs =
        (struct trap_frame *)((uint64_t)child->kernel_stack + tf_off);
    if (parent->user_stack != (void *)0) {
        uint64_t usp_off = regs->x[TF_SP] - (uint64_t)parent->user_stack;
        child_regs->x[TF_SP] = (uint64_t)child->user_stack + usp_off;
    }
    child_regs->x[TF_A0] = 0;
    child->context.ra = (uint64_t)ret_from_exception;
    child->context.sp = (uint64_t)child_regs;

    list_add_tail(&child->all_list, &all_threads);
    list_add_tail(&child->list, &run_queue);
    return child->pid;
}

int process_exec_image(const void *image, unsigned long size) {
    struct thread *cur = get_current();
    if (cur == (void *)0 || cur->kernel_stack == (void *)0 ||
        image == (void *)0 || size == 0 || size > USER_IMAGE_SIZE) {
        return -1;
    }
    if (cur->user_stack == (void *)0) {
        cur->user_stack = allocate(USER_STACK_SIZE);
        if (cur->user_stack == (void *)0) {
            return -1;
        }
    }

    memcpy((void *)USER_IMAGE_BASE, image, size);
    memset(cur->user_stack, 0, USER_STACK_SIZE);
    struct trap_frame *regs = (struct trap_frame *)((uint64_t)cur->kernel_stack +
                                                    THREAD_STACK_SIZE -
                                                    sizeof(struct trap_frame));
    memset(regs, 0, sizeof(*regs));
    cur->pending_signals = 0;
    cur->processing_signal = 0;
    if (cur->signal_stack != (void *)0) {
        free(cur->signal_stack);
        cur->signal_stack = (void *)0;
    }
    regs->x[TF_EPC] = USER_IMAGE_BASE;
    regs->x[TF_SP] = (uint64_t)cur->user_stack + USER_STACK_SIZE;
    regs->x[TF_STATUS] = (1UL << 5);
    cur->context.ra = (uint64_t)ret_from_exception;
    cur->context.sp = (uint64_t)regs;
    return 0;
}

int process_spawn_user(const void *image, unsigned long size) {
    if (image == (void *)0 || size == 0 || size > USER_IMAGE_SIZE) {
        return -1;
    }
    struct thread *t = alloc_thread((void (*)(void))ret_from_exception);
    if (t == (void *)0) {
        return -1;
    }
    t->user_stack = allocate(USER_STACK_SIZE);
    if (t->user_stack == (void *)0) {
        free_thread(t);
        return -1;
    }
    t->parent = get_current();
    memcpy((void *)USER_IMAGE_BASE, image, size);
    struct trap_frame *regs = (struct trap_frame *)((uint64_t)t->kernel_stack +
                                                    THREAD_STACK_SIZE -
                                                    sizeof(struct trap_frame));
    memset(regs, 0, sizeof(*regs));
    regs->x[TF_EPC] = USER_IMAGE_BASE;
    regs->x[TF_SP] = (uint64_t)t->user_stack + USER_STACK_SIZE;
    regs->x[TF_STATUS] = (1UL << 5);
    t->context.ra = (uint64_t)ret_from_exception;
    t->context.sp = (uint64_t)regs;
    list_add_tail(&t->list, &run_queue);
    return t->pid;
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
        if (t->parent != (void *)0 && t->parent->state != THREAD_ZOMBIE) {
            list_add_tail(&t->list, &zombie_queue);
            continue;
        }
        free_thread(t);
    }
}

void idle(void) {
    while (1) {
        kill_zombies();
        schedule();
    }
}
