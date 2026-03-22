#include <stdarg.h>
extern void uart_init(unsigned long base);
extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char *s);
extern void uart_hex(unsigned long h);
extern void load_kernel(void);
extern int fdt_path_offset(const void *fdt, const char *target_path);
extern const void *fdt_getprop(const void *fdt, int nodeoffset,
                               const char *name, int *lenp);

static inline unsigned long bswap64(unsigned long x) {
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8) |
           ((x & 0x000000FF00000000ULL) >> 8) |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
}

void uart_dec(int num) {
    if (num == 0) {
        uart_putc('0');
        return;
    }

    // 處理負數
    unsigned int unum;
    if (num < 0) {
        uart_putc('-');
        unum = (unsigned int)(-num);
    } else {
        unum = (unsigned int)num;
    }

    // 32 位元整數最多 10 個位數，加上 sign，給 16 個 bytes 的 buffer 絕對夠用
    char buf[16];
    int i = 0;

    // 依序取出個位數、十位數、百位數...
    while (unum > 0) {
        buf[i++] = (unum % 10) + '0'; // 加上 '0' 轉換成 ASCII 字元
        unum /= 10;
    }

    // 因為是從低位數開始存的，所以要反過來印
    while (i > 0) {
        i--;
        uart_putc(buf[i]);
    }
}

void printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt); // Initialize the argument list

    while (*fmt != '\0') {
        if (*fmt == '%') {
            fmt++; // Skip the '%'
            switch (*fmt) {
            case 's': {
                char *s = va_arg(args, char *);
                uart_puts(s);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                uart_putc(c);
                break;
            }
            case 'x': {
                unsigned long x = va_arg(args, unsigned long);
                uart_hex(x);
                break;
            }
            case 'd': {
                int d = va_arg(args, int);
                uart_dec(d);
                break;
            }
            case '%': {
                uart_putc('%');
                break;
            }
            default: {
                uart_putc('%');
                uart_putc(*fmt);
                break;
            }
            }
        } else {
            uart_putc(*fmt);
        }
        fmt++; // Move to the next character
    }

    va_end(args);
}

#define SBI_EXT_BASE 0x10

enum sbi_ext_base_fid {
    SBI_EXT_BASE_GET_SPEC_VERSION,
    SBI_EXT_BASE_GET_IMP_ID,
    SBI_EXT_BASE_GET_IMP_VERSION,
    SBI_EXT_BASE_PROBE_EXT,
    SBI_EXT_BASE_GET_MVENDORID,
    SBI_EXT_BASE_GET_MARCHID,
    SBI_EXT_BASE_GET_MIMPID,
};

struct sbiret {
    long error;
    long value;
};

struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0,
                        unsigned long arg1, unsigned long arg2,
                        unsigned long arg3, unsigned long arg4,
                        unsigned long arg5) {
    struct sbiret ret;
    register unsigned long a0 asm("a0") = (unsigned long)arg0;
    register unsigned long a1 asm("a1") = (unsigned long)arg1;
    register unsigned long a2 asm("a2") = (unsigned long)arg2;
    register unsigned long a3 asm("a3") = (unsigned long)arg3;
    register unsigned long a4 asm("a4") = (unsigned long)arg4;
    register unsigned long a5 asm("a5") = (unsigned long)arg5;
    register unsigned long a6 asm("a6") = (unsigned long)fid;
    register unsigned long a7 asm("a7") = (unsigned long)ext;
    asm volatile("ecall"
                 : "+r"(a0), "+r"(a1)
                 : "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                 : "memory");
    ret.error = a0;
    ret.value = a1;
    return ret;
}

/**
 * sbi_get_spec_version() - Get the SBI specification version.
 *
 * Return: The current SBI specification version.
 * The minor number of the SBI specification is encoded in the low 24 bits,
 * with the major number encoded in the next 7 bits. Bit 31 must be 0.
 */
long sbi_get_spec_version(void) {
    struct sbiret result = sbi_ecall(
        SBI_EXT_BASE, SBI_EXT_BASE_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);
    if (result.error) {
        return result.error;
    }
    return result.value;
}

long sbi_get_impl_id() {
    struct sbiret result =
        sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_ID, 0, 0, 0, 0, 0, 0);
    if (result.error) {
        return result.error;
    }
    return result.value;
}

long sbi_get_impl_version() {
    struct sbiret result =
        sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_VERSION, 0, 0, 0, 0, 0, 0);
    if (result.error) {
        return result.error;
    }
    return result.value;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        s1++;
        s2++;
    }
    // Return the difference; if the strings are identical, the result is 0
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

void start_kernel(void *dtb_base) {
    // Parse DTB to get UART base address and initialize UART
    // QEMU uses "/soc/serial", OrangePi RV2 uses "/soc/serial"
    int offset = fdt_path_offset(dtb_base, "/soc/serial");
    if (offset >= 0) {
        int len;
        const void *reg = fdt_getprop(dtb_base, offset, "reg", &len);
        if (reg && len >= 8) {
            unsigned long uart_addr = bswap64(*(const unsigned long *)reg);
            uart_init(uart_addr);
        }
    }

    uart_puts("\nStarting kernel ...\n");
#define MAX_CMD_LEN 128
    char cmd_buf[MAX_CMD_LEN];
    int cmd_idx = 0;

    while (1) {
        uart_puts("opi-rv2> ");
        cmd_idx = 0;

        // 2. Inner loop: receive input until Enter is pressed
        while (1) {
            char c = uart_getc();

            if (c == '\r' || c == '\n') {
                // Enter received: print newline and append '\0' to form a valid
                // C string
                uart_puts("\r\n");
                cmd_buf[cmd_idx] = '\0';
                break;
            } else if (c == '\b' || c == '\x7f') {
                // Backspace or Delete received
                if (cmd_idx > 0) {
                    cmd_idx--;
                    // Move cursor back, print space to overwrite, move back
                    // again
                    uart_puts("\b \b");
                }
            } else if (cmd_idx < MAX_CMD_LEN - 1) {
                cmd_buf[cmd_idx++] = c;
                uart_putc(c);
            }
        }

        if (cmd_idx > 0) {
            if (strcmp(cmd_buf, "help") == 0) {
                printf("Available commands:\r\n"
                       "  help  - show all commands.\r\n"
                       "  hello - print hello world.\r\n"
                       "  info  - print system info.\r\n");
            } else if (strcmp(cmd_buf, "hello") == 0) {
                printf("Hello world.\r\n");
            } else if (strcmp(cmd_buf, "info") == 0) {
                printf("System information:\r\n");
                printf("  OpenSBI specification version: %x\r\n",
                       sbi_get_spec_version());
                printf("  implementation ID: %x\r\n", sbi_get_impl_id());
                printf("  implementation version: %x\r\n",
                       sbi_get_impl_version());
            } else if (strcmp(cmd_buf, "load") == 0) {
                load_kernel();
            } else if (strcmp(cmd_buf, "ls") == 0) {
                load_kernel();
            } else {
                printf("Unknown command: %s\r\n", cmd_buf);
                printf("Use help to get commands\r\n");
            }
        }
    }
}