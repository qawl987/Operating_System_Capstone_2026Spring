#include "process.h"

#include "config.h"
#include "helper.h"
#include "kmalloc.h"
#include "mmap.h"
#include "signal.h"
#include "thread.h"
#include "vm.h"
#include "vfs.h"

extern void ret_from_exception(void);

#define TF_SP 1
#define TF_A0 9
#define TF_EPC 31
#define TF_STATUS 32

static int is_child_of_current(struct thread *t) {
    return t != (void *)0 && t->parent == get_current();
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
        struct thread *target = thread_find_zombie_child(pid);
        if (target != (void *)0) {
            long ret = target->pid;
            printf("[INFO] Killing zombie thread with PID: %d\n", target->pid);
            thread_free(target);
            return ret;
        }

        if (pid >= 0) {
            target = thread_find((int)pid);
            if (target == (void *)0 || !is_child_of_current(target)) {
                return -1;
            }
        }
        schedule();
    }
}

int process_stop(long pid) {
    struct thread *target = thread_find((int)pid);
    if (target == (void *)0 || thread_is_idle(target) ||
        target->state == THREAD_ZOMBIE) {
        return -1;
    }
    if (target == get_current()) {
        process_exit(0);
        return 0;
    }
    target->state = THREAD_ZOMBIE;
    target->parent = (void *)0;
    printf("[INFO] Killing zombie thread with PID: %d\n", target->pid);
    thread_make_zombie(target);
    return 0;
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

long process_fork(struct trap_frame *regs) {
    struct thread *parent = get_current();
    if (parent == (void *)0 || parent->kernel_stack == (void *)0 ||
        parent->pgd == (void *)0 || regs == (void *)0) {
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

    memcpy(child, parent, sizeof(*child));
    memcpy(kstack, parent->kernel_stack, THREAD_STACK_SIZE);

    child->pid = thread_alloc_pid();
    child->state = THREAD_RUNNING;
    child->exit_code = 0;
    child->wake_time = 0;
    child->pending_signals = 0;
    child->processing_signal = 0;
    child->signal_stack = (void *)0;
    child->kernel_stack = kstack;
    child->parent = parent;
    child->pgd = vm_create_user_pgd();
    child->user_stack = (void *)0;
    INIT_LIST_HEAD(&child->list);
    INIT_LIST_HEAD(&child->all_list);
    if (child->pgd == (void *)0) {
        free(kstack);
        free(child);
        return -1;
    }

    if (vm_clone_user_cow(child->pgd, parent->pgd) < 0) {
        vm_free_user_pgd(child->pgd);
        free(kstack);
        free(child);
        return -1;
    }
    child->user_stack = (void *)0;
    // child files clone above, so refcnt++ here
    for (int i = 0; i < VFS_MAX_FD; i++) {
        vfs_file_get(child->files[i]);
    }

    uint64_t tf_off = (uint64_t)regs - (uint64_t)parent->kernel_stack;
    struct trap_frame *child_regs =
        (struct trap_frame *)((uint64_t)child->kernel_stack + tf_off);
    child_regs->x[TF_SP] = regs->x[TF_SP];
    child_regs->x[TF_A0] = 0;
    child->context.ra = (uint64_t)ret_from_exception;
    child->context.sp = (uint64_t)child_regs;

    thread_add_to_all(child);
    thread_make_runnable(child);
    return child->pid;
}

int process_exec_image(const void *image, unsigned long size) {
    struct thread *cur = get_current();
    if (cur == (void *)0 || cur->kernel_stack == (void *)0) {
        return -1;
    }

    unsigned long *old_pgd = cur->pgd;
    if (process_install_user_image(cur, image, size) < 0) {
        return -1;
    }
    // get kernel stack top, and get trap_frame start address
    struct trap_frame *regs = (struct trap_frame *)((uint64_t)cur->kernel_stack +
                                                    THREAD_STACK_SIZE -
                                                    sizeof(struct trap_frame));
    memset(regs, 0, sizeof(*regs));
    process_clear_signal_state();
    regs->x[TF_EPC] = USER_TEXT_VA;
    // 8GB
    regs->x[TF_SP] = USER_STACK_TOP;
    regs->x[TF_STATUS] = (1UL << 5);
    // cur->context.ra/sp ensures that when the scheduler context-switches 
    // back to this thread, it can resume from the same trap frame and 
    // return to user space via ret_from_exception.
    cur->context.ra = (uint64_t)ret_from_exception;
    cur->context.sp = (uint64_t)regs;
    vm_switch(cur->pgd);
    if (old_pgd != cur->pgd) {
        vm_free_user_pgd(old_pgd);
    }
    return 0;
}

int process_spawn_user(const void *image, unsigned long size) {
    struct thread *t = thread_alloc((void (*)(void))ret_from_exception);
    if (t == (void *)0 || process_install_user_image(t, image, size) < 0) {
        if (t != (void *)0) {
            thread_free(t);
        }
        return -1;
    }
    t->parent = get_current();
    struct trap_frame *regs = (struct trap_frame *)((uint64_t)t->kernel_stack +
                                                    THREAD_STACK_SIZE -
                                                    sizeof(struct trap_frame));
    memset(regs, 0, sizeof(*regs));
    regs->x[TF_EPC] = USER_TEXT_VA;
    regs->x[TF_SP] = USER_STACK_TOP;
    regs->x[TF_STATUS] = (1UL << 5);
    t->context.ra = (uint64_t)ret_from_exception;
    t->context.sp = (uint64_t)regs;
    thread_make_runnable(t);
    return t->pid;
}
