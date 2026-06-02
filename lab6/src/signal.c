#include "signal.h"

#include "helper.h"
#include "kmalloc.h"
#include "process.h"
#include "thread.h"
#include "vm.h"

#define TF_SP 1
#define TF_RA 0
#define TF_EPC 31
#define SIGNAL_TRAMPOLINE_SIZE 16UL

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

void process_clear_signal_state(void) {
    struct thread *cur = get_current();
    if (cur == (void *)0) {
        return;
    }
    cur->pending_signals = 0;
    cur->processing_signal = 0;
    if (cur->signal_stack != (void *)0) {
        free(cur->signal_stack);
        cur->signal_stack = (void *)0;
    }
}

long process_kill(int pid, int signum) {
    if (signum < 0 || signum >= SIGNAL_MAX) {
        return -1;
    }
    struct thread *target = thread_find(pid);
    if (target == (void *)0 || thread_is_idle(target) ||
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
        thread_make_runnable(target);
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
    memset(stack, 0, USER_STACK_SIZE);
    if (vm_map_pages(cur->pgd, USER_SIGNAL_STACK_PAGE_VA, USER_STACK_SIZE,
                     virt_to_phys((unsigned long)stack), PROT_USER_RWX) < 0) {
        free(stack);
        cur->pending_signals |= (1U << signum);
        return;
    }
    // USER_SIGNAL_STACK_TOP
    // tramp_user                -> tramp_kernel va
    // ------
    // USER_SIGNAL_STACK_PAGE_VA -> stack
    memcpy(&cur->backup_trap_frame, regs, sizeof(*regs));
    uint64_t tramp_user = (USER_SIGNAL_STACK_TOP - SIGNAL_TRAMPOLINE_SIZE) &
                          ~0xFUL;
    uint64_t tramp_kernel =
        (uint64_t)stack + (tramp_user - USER_SIGNAL_STACK_PAGE_VA);
    uint32_t *code = (uint32_t *)tramp_kernel;
    // li a7, 11
    // ecall
    code[0] = 0x00b00893U;
    code[1] = 0x00000073U;
    asm volatile(".word 0x0000100f" ::: "memory");

    cur->signal_stack = stack;
    cur->processing_signal = 1;
    regs->x[TF_EPC] = (uint64_t)handler;
    regs->x[TF_SP] = tramp_user;
    regs->x[TF_RA] = tramp_user;
}
