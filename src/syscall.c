#include "syscall.h"

#include "config.h"
#include "framebuffer.h"
#include "helper.h"
#include "initrd.h"
#include "kmalloc.h"
#include "mmap.h"
#include "process.h"
#include "signal.h"
#include "thread.h"
#include "uart.h"
#include "vfs.h"

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
    SYS_MMAP = 13,
    SYS_OPEN = 14,
    SYS_CLOSE = 15,
    SYS_READ = 16,
    SYS_WRITE = 17,
    SYS_MKDIR = 18,
    SYS_MOUNT = 19,
    SYS_CHDIR = 20,
    SYS_LSEEK64 = 21,
    SYS_IOCTL = 22,
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

static int fd_alloc(struct thread *task, struct file *file) {
    if (task == (void *)0 || file == (void *)0) {
        return -1;
    }
    for (int i = 0; i < VFS_MAX_FD; i++) {
        if (task->files[i] == (void *)0) {
            task->files[i] = file;
            return i;
        }
    }
    return -1;
}

static struct file *fd_get(struct thread *task, int fd) {
    if (task == (void *)0 || fd < 0 || fd >= VFS_MAX_FD) {
        return (void *)0;
    }
    return task->files[fd];
}

static long sys_open(const char *path, int flags) {
    struct thread *cur = get_current();
    struct file *file = (void *)0;
    if (path == (void *)0 ||
        vfs_open_at(cur->fs_root, cur->cwd, path, flags, &file) < 0) {
        return -1;
    }
    int fd = fd_alloc(cur, file);
    if (fd < 0) {
        vfs_close(file);
        return -1;
    }
    return fd;
}

static long sys_close(int fd) {
    struct thread *cur = get_current();
    struct file *file = fd_get(cur, fd);
    if (file == (void *)0) {
        return -1;
    }
    cur->files[fd] = (void *)0;
    return vfs_close(file);
}

static long sys_read(int fd, void *buf, unsigned long count) {
    struct file *file = fd_get(get_current(), fd);
    if (file == (void *)0) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (buf == (void *)0) {
        return -1;
    }
    return vfs_read(file, buf, count);
}

static long sys_write(int fd, const void *buf, unsigned long count) {
    struct file *file = fd_get(get_current(), fd);
    if (file == (void *)0) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (buf == (void *)0) {
        return -1;
    }
    return vfs_write(file, buf, count);
}

static long sys_lseek64(int fd, long offset, int whence) {
    struct file *file = fd_get(get_current(), fd);
    if (file == (void *)0) {
        return -1;
    }
    return vfs_lseek64(file, offset, whence);
}

static long sys_ioctl(int fd, unsigned long request, void *arg) {
    struct file *file = fd_get(get_current(), fd);
    if (file == (void *)0) {
        return -1;
    }
    return vfs_ioctl(file, request, arg);
}

static long sys_mkdir(const char *path) {
    struct thread *cur = get_current();
    return vfs_mkdir_at(cur->fs_root, cur->cwd, path);
}

static long sys_mount(const char *target, const char *filesystem) {
    struct thread *cur = get_current();
    return vfs_mount_at(cur->fs_root, cur->cwd, target, filesystem);
}

static long sys_exec_path(const char *path) {
    if (path == (void *)0) {
        return -1;
    }
    struct file *file = (void *)0;
    struct thread *cur = get_current();
    if (vfs_open_at(cur->fs_root, cur->cwd, path, 0, &file) < 0) {
        char fallback[VFS_MAX_PATH + 1];
        if (path[0] == '/' || strlen(path) + 7 > VFS_MAX_PATH) {
            return -1;
        }
        strncpy(fallback, "/ramfs/", sizeof(fallback));
        strncpy(fallback + 7, path, sizeof(fallback) - 7);
        fallback[VFS_MAX_PATH] = '\0';
        if (vfs_open_at(cur->fs_root, cur->cwd, fallback, 0, &file) < 0) {
            return -1;
        }
    }

    void *image = allocate(USER_IMAGE_SIZE);
    if (image == (void *)0) {
        vfs_close(file);
        return -1;
    }
    unsigned long size = 0;
    while (size < USER_IMAGE_SIZE) {
        long n = vfs_read(file, (char *)image + size, USER_IMAGE_SIZE - size);
        if (n < 0) {
            vfs_close(file);
            free(image);
            return -1;
        }
        if (n == 0) {
            break;
        }
        size += (unsigned long)n;
    }
    vfs_close(file);
    if (size == 0) {
        free(image);
        return -1;
    }
    return process_exec_image(image, size);
}

void syscall_handler(struct pt_regs *regs) {
    long ret = -1;
    if (regs == (void *)0) {
        return;
    }

    asm volatile("csrsi sstatus, 2\n"
                 "li t0, (1 << 18)\n"
                 "csrs sstatus, t0"
                 :
                 :
                 : "memory", "t0");

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
        ret = sys_exec_path((const char *)regs->a0);
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
    case SYS_MMAP:
        ret = process_mmap((void *)regs->a0, (unsigned long)regs->a1,
                           (int)regs->a2, (int)regs->a3);
        break;
    case SYS_OPEN:
        ret = sys_open((const char *)regs->a0, (int)regs->a1);
        break;
    case SYS_CLOSE:
        ret = sys_close((int)regs->a0);
        break;
    case SYS_READ:
        ret = sys_read((int)regs->a0, (void *)regs->a1,
                       (unsigned long)regs->a2);
        break;
    case SYS_WRITE:
        ret = sys_write((int)regs->a0, (const void *)regs->a1,
                        (unsigned long)regs->a2);
        break;
    case SYS_MKDIR:
        ret = sys_mkdir((const char *)regs->a0);
        break;
    case SYS_MOUNT:
        ret = sys_mount((const char *)regs->a1, (const char *)regs->a2);
        break;
    case SYS_CHDIR:
        ret = vfs_chdir(get_current(), (const char *)regs->a0);
        break;
    case SYS_LSEEK64:
        ret = sys_lseek64((int)regs->a0, (long)regs->a1, (int)regs->a2);
        break;
    case SYS_IOCTL:
        ret = sys_ioctl((int)regs->a0, (unsigned long)regs->a1,
                        (void *)regs->a2);
        break;
    default:
        ret = -1;
        break;
    }

    regs->a0 = (unsigned long)ret;
    check_pending_signals((struct trap_frame *)regs);
}
