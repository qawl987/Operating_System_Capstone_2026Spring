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
#include "vm.h"

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

static int user_page_ptr(const void *user, int write, void **kernel,
                         size_t *avail) {
    struct thread *cur = get_current();
    unsigned long va = (unsigned long)user;
    // no user page table, over user space
    if (cur == (void *)0 || cur->pgd == (void *)0 || user == (void *)0 ||
        va >= USER_STACK_TOP) {
        return -1;
    }
    // get last level pte
    unsigned long *pte = vm_get_pte(cur->pgd, va);
    if (pte == (void *)0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
        (write && (*pte & PTE_W) == 0) ||
        (!write && (*pte & PTE_R) == 0)) {
        // try to fix lazy mmap
        if (process_handle_page_fault(va, write ? 15 : 13) < 0) {
            return -1;
        }
        pte = vm_get_pte(cur->pgd, va);
    }
    // check again after handle page fault
    if (pte == (void *)0 || (*pte & PTE_V) == 0 || (*pte & PTE_U) == 0 ||
        (write && (*pte & PTE_W) == 0) ||
        (!write && (*pte & PTE_R) == 0)) {
        return -1;
    }

    unsigned long off = va & (VM_PAGE_SIZE - 1);
    // combine physical page address and offset to get pa
    unsigned long pa = ((*pte >> 10) << 12) + off;
    // translate pa to high addres(kernel address)
    *kernel = (void *)phys_to_virt(pa);
    // count how many bytes can read remain in this page
    *avail = VM_PAGE_SIZE - off;
    if (*avail > USER_STACK_TOP - va) {
        *avail = USER_STACK_TOP - va;
    }
    return 0;
}

static int user_check(const void *user, size_t len, int write) {
    size_t done = 0;
    if (len == 0) {
        return 0;
    }
    if (user == (void *)0 || (unsigned long)user >= USER_STACK_TOP ||
        len > USER_STACK_TOP - (unsigned long)user) {
        return -1;
    }
    while (done < len) {
        void *kernel = (void *)0;
        size_t avail = 0;
        if (user_page_ptr((const char *)user + done, write, &kernel, &avail) <
            0) {
            return -1;
        }
        (void)kernel;
        done += avail;
    }
    return 0;
}

static int copy_from_user(void *dst, const void *src, size_t len) {
    size_t done = 0;
    if (len == 0) {
        return 0;
    }
    if (dst == (void *)0 || src == (void *)0 ||
        (unsigned long)src >= USER_STACK_TOP ||
        len > USER_STACK_TOP - (unsigned long)src) {
        return -1;
    }
    while (done < len) {
        void *kernel = (void *)0;
        size_t avail = 0;
        // translate user address to kernel address
        if (user_page_ptr((const char *)src + done, 0, &kernel, &avail) < 0) {
            return -1;
        }
        if (avail > len - done) {
            avail = len - done;
        }
        // copy string inside kernel VA
        memcpy((char *)dst + done, kernel, avail);
        done += avail;
    }
    return 0;
}

static int copy_to_user(void *dst, const void *src, size_t len) {
    size_t done = 0;
    if (len == 0) {
        return 0;
    }
    if (dst == (void *)0 || src == (void *)0 ||
        (unsigned long)dst >= USER_STACK_TOP ||
        len > USER_STACK_TOP - (unsigned long)dst) {
        return -1;
    }
    while (done < len) {
        void *kernel = (void *)0;
        size_t avail = 0;
        // kernel side user buffer address
        if (user_page_ptr((char *)dst + done, 1, &kernel, &avail) < 0) {
            return -1;
        }
        if (avail > len - done) {
            avail = len - done;
        }
        memcpy(kernel, (const char *)src + done, avail);
        done += avail;
    }
    return 0;
}

static int copy_string_from_user(char *dst, const char *src, size_t max) {
    if (dst == (void *)0 || src == (void *)0 || max == 0) {
        return -1;
    }
    for (size_t i = 0; i < max; i++) {
        if (copy_from_user(&dst[i], src + i, 1) < 0) {
            return -1;
        }
        if (dst[i] == '\0') {
            return 0;
        }
    }
    return -1;
}

static long sys_uart_read(char *buf, long count) {
    if (buf == (void *)0 || count < 0) {
        return -1;
    }
    if (user_check(buf, (size_t)count, 1) < 0) {
        return -1;
    }
    for (long i = 0; i < count; i++) {
        char ch;
        while (uart_try_getc(&ch) < 0) {
            schedule();
        }
        if (copy_to_user(buf + i, &ch, 1) < 0) {
            return i == 0 ? -1 : i;
        }
    }
    return count;
}

static long sys_uart_write(const char *buf, long count) {
    if (buf == (void *)0 || count < 0) {
        return -1;
    }
    unsigned char tmp[512];
    long done = 0;
    while (done < count) {
        size_t n = (size_t)(count - done);
        if (n > sizeof(tmp)) {
            n = sizeof(tmp);
        }
        if (copy_from_user(tmp, buf + done, n) < 0) {
            return done == 0 ? -1 : done;
        }
        for (size_t i = 0; i < n; i++) {
            uart_putc(tmp[i]);
        }
        done += (long)n;
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
    char kpath[VFS_MAX_PATH + 1];
    if (copy_string_from_user(kpath, path, sizeof(kpath)) < 0 ||
        vfs_open_at(cur->fs_root, cur->cwd, kpath, flags, &file) < 0) {
        return -1;
    }
    // find first not use fd index
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
    unsigned char tmp[512];
    unsigned long done = 0;
    if (file == (void *)0) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    if (user_check(buf, count, 1) < 0) {
        return -1;
    }
    while (done < count) {
        size_t n = count - done;
        if (n > sizeof(tmp)) {
            n = sizeof(tmp);
        }
        long ret = vfs_read(file, tmp, n);
        // fail, return part of len
        if (ret < 0) {
            return done == 0 ? ret : (long)done;
        }
        // no more to read
        if (ret == 0) {
            break;
        }
        if (copy_to_user((char *)buf + done, tmp, (size_t)ret) < 0) {
            return done == 0 ? -1 : (long)done;
        }
        done += (unsigned long)ret;
        // no more file can read
        if ((size_t)ret < n) {
            break;
        }
    }
    return (long)done;
}

static long sys_write(int fd, const void *buf, unsigned long count) {
    struct file *file = fd_get(get_current(), fd);
    unsigned char tmp[512];
    unsigned long done = 0;
    if (file == (void *)0) {
        return -1;
    }
    if (count == 0) {
        return 0;
    }
    while (done < count) {
        size_t n = count - done;
        if (n > sizeof(tmp)) {
            n = sizeof(tmp);
        }
        if (copy_from_user(tmp, (const char *)buf + done, n) < 0) {
            return done == 0 ? -1 : (long)done;
        }
        long ret = vfs_write(file, tmp, n);
        if (ret < 0) {
            return done == 0 ? ret : (long)done;
        }
        if (ret == 0) {
            break;
        }
        done += (unsigned long)ret;
        if ((size_t)ret < n) {
            break;
        }
    }
    return (long)done;
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
    if (request == 0) {
        struct framebuffer_info info;
        long ret = vfs_ioctl(file, request, &info);
        if (ret < 0) {
            return ret;
        }
        return copy_to_user(arg, &info, sizeof(info));
    }
    return vfs_ioctl(file, request, (void *)0);
}

static long sys_mkdir(const char *path) {
    struct thread *cur = get_current();
    char kpath[VFS_MAX_PATH + 1];
    if (copy_string_from_user(kpath, path, sizeof(kpath)) < 0) {
        return -1;
    }
    return vfs_mkdir_at(cur->fs_root, cur->cwd, kpath);
}

static long sys_mount(const char *target, const char *filesystem) {
    struct thread *cur = get_current();
    char ktarget[VFS_MAX_PATH + 1];
    char kfs[VFS_MAX_NAME + 1];
    if (copy_string_from_user(ktarget, target, sizeof(ktarget)) < 0 ||
        copy_string_from_user(kfs, filesystem, sizeof(kfs)) < 0) {
        return -1;
    }
    return vfs_mount_at(cur->fs_root, cur->cwd, ktarget, kfs);
}

static long sys_chdir(const char *path) {
    char kpath[VFS_MAX_PATH + 1];
    if (copy_string_from_user(kpath, path, sizeof(kpath)) < 0) {
        return -1;
    }
    return vfs_chdir(get_current(), kpath);
}

static long sys_exec_path(const char *path) {
    char kpath[VFS_MAX_PATH + 1];
    if (copy_string_from_user(kpath, path, sizeof(kpath)) < 0) {
        return -1;
    }
    struct file *file = (void *)0;
    struct thread *cur = get_current();
    if (vfs_open_at(cur->fs_root, cur->cwd, kpath, 0, &file) < 0) {
        char fallback[VFS_MAX_PATH + 1];
        if (kpath[0] == '/' || strlen(kpath) + 7 > VFS_MAX_PATH) {
            return -1;
        }
        strncpy(fallback, "/ramfs/", sizeof(fallback));
        strncpy(fallback + 7, kpath, sizeof(fallback) - 7);
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

static long sys_display(const unsigned int *bmp_image, unsigned int width,
                        unsigned int height) {
    unsigned long pixels;
    unsigned long bytes;
    if (bmp_image == (void *)0 || width == 0 || height == 0 ||
        width > FRAMEBUFFER_WIDTH || height > FRAMEBUFFER_HEIGHT) {
        return -1;
    }
    pixels = (unsigned long)width * (unsigned long)height;
    if (pixels > FRAMEBUFFER_SIZE / sizeof(unsigned int)) {
        return -1;
    }
    bytes = pixels * sizeof(unsigned int);
    if (user_check(bmp_image, bytes, 0) < 0) {
        return -1;
    }
    return framebuffer_display(bmp_image, width, height);
}

void syscall_handler(struct pt_regs *regs) {
    long ret = -1;
    if (regs == (void *)0) {
        return;
    }

    enable_sstatus_sie();
    asm volatile("li t0, (1 << 18)\n"
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
        ret = sys_display((const unsigned int *)regs->a0,
                          (unsigned int)regs->a1, (unsigned int)regs->a2);
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
        ret = sys_chdir((const char *)regs->a0);
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
