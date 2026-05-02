#include "syscall.h"

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
        buf[i] = uart_getc();
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
    default:
        ret = -1;
        break;
    }

    regs->a0 = (unsigned long)ret;
}
