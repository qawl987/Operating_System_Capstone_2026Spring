#include "syscall.h"

#include "framebuffer.h"
#include "initrd.h"
#include "thread.h"
#include "uart.h"

enum {
    SYS_GETPID = 0,
    SYS_UART_READ = 1,
    SYS_UART_WRITE = 2,
    SYS_EXEC = 3,
    SYS_FORK = 4,
    SYS_WAITPID = 5,
    SYS_EXIT = 6,
    SYS_STOP = 7,
    SYS_DISPLAY = 8,
    SYS_USLEEP = 9,
    SYS_SIGNAL = 10,
    SYS_SIGRETURN = 11,
    SYS_KILL = 12,
};

static unsigned long initrd_start;
static unsigned long initrd_end;

void syscall_set_initrd(unsigned long start, unsigned long end) {
    initrd_start = start;
    initrd_end = end;
}

static long sys_uart_read(char *buf, long count) {
    if (buf == (void *)0 || count < 0) {
        return -1;
    }
    for (long i = 0; i < count; i++) {
        while (uart_try_getc(&buf[i]) < 0) {
            schedule();
        }
    }
    return count;
}

static long sys_uart_write(const char *buf, long count) {
    if (buf == (void *)0 || count < 0) {
        return -1;
    }
    for (long i = 0; i < count; i++) {
        uart_putc(buf[i]);
    }
    return count;
}

static long sys_exec(const char *path) {
    if (path == (void *)0 || initrd_start == 0 || initrd_end == 0) {
        return -1;
    }
    size_t size = 0;
    const void *image =
        initrd_find_file((void *)initrd_start, (void *)initrd_end, path, &size);
    if (image == (void *)0) {
        return -1;
    }
    return process_exec_image(image, (unsigned long)size);
}

void syscall_handler(struct pt_regs *regs) {
    long ret = -1;
    if (regs == (void *)0) {
        return;
    }

    asm volatile("csrsi sstatus, 2");

    switch (regs->a7) {
    case SYS_GETPID:
        ret = get_current()->pid;
        break;
    case SYS_UART_READ:
        ret = sys_uart_read((char *)regs->a0, (long)regs->a1);
        break;
    case SYS_UART_WRITE:
        ret = sys_uart_write((const char *)regs->a0, (long)regs->a1);
        break;
    case SYS_EXEC:
        ret = sys_exec((const char *)regs->a0);
        break;
    case SYS_FORK:
        ret = process_fork((struct trap_frame *)regs);
        break;
    case SYS_WAITPID:
        ret = process_waitpid((long)regs->a0);
        break;
    case SYS_EXIT:
        process_exit((int)regs->a0);
        ret = 0;
        break;
    case SYS_STOP:
        ret = process_stop((long)regs->a0);
        break;
    case SYS_DISPLAY:
        ret = framebuffer_display((const unsigned int *)regs->a0,
                                  (unsigned int)regs->a1,
                                  (unsigned int)regs->a2);
        break;
    case SYS_USLEEP:
        ret = process_usleep((unsigned int)regs->a0);
        break;
    case SYS_SIGNAL:
        ret = process_signal((int)regs->a0, (void (*)(void))regs->a1);
        break;
    case SYS_SIGRETURN:
        process_sigreturn((struct trap_frame *)regs);
        return;
    case SYS_KILL:
        ret = process_kill((int)regs->a0, (int)regs->a1);
        if (ret == 0) {
            schedule();
        }
        break;
    default:
        ret = -1;
        break;
    }

    regs->a0 = (unsigned long)ret;
    check_pending_signals((struct trap_frame *)regs);
}
