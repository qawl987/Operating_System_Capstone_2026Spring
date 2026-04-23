#include "bootloader.h"
#include "buddy.h"
#include "config.h"
#include "dtbParser.h"
#include "helper.h"
#include "initrd.h"
#include "kmalloc.h"
#include "sbi.h"
#include "startup_alloc.h"
#include "timer.h"
#include "trap.h"
#include "uart.h"

/* Global state for initrd addresses */
static unsigned long g_initrd_start = 0;
static unsigned long g_initrd_end = 0;

struct timeout_msg {
    uint64_t command_time;
    int sec;
    char msg[96];
};

static int parse_dec(const char *s, int *value) {
    int v = 0;
    int i = 0;
    if (s == (void *)0 || value == (void *)0 || s[0] == '\0') {
        return -1;
    }
    while (s[i] != '\0') {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        v = v * 10 + (s[i] - '0');
        i++;
    }
    *value = v;
    return 0;
}

static void timeout_cb(void *arg) {
    struct timeout_msg *t = (struct timeout_msg *)arg;
    printf("[setTimeout] now=%d cmd=%d +%d \"%s\"\r\n",
           (int)trap_uptime_seconds(), (int)t->command_time, t->sec, t->msg);
    free(t);
}

static void task_test_cb(void *arg) {
    const char *label = (const char *)arg;
    printf("[Task] Executing Priority %s\r\n", label);
}

static void nested_preempt_hi_cb(void *arg) {
    (void)arg;
    printf("[NestedDemo][HI] preempt task start (prio=9) at %d sec\r\n",
           (int)trap_uptime_seconds());
    for (volatile unsigned long i = 0; i < 600000UL; i++) {
        asm volatile("" ::: "memory");
    }
    printf("[NestedDemo][HI] preempt task done at %d sec\r\n",
           (int)trap_uptime_seconds());
}

static void nested_timer_kick_cb(void *arg) {
    (void)arg;
    printf("[NestedDemo][TIMER] irq callback at %d sec, queue HI task\r\n",
           (int)trap_uptime_seconds());
    add_task(nested_preempt_hi_cb, (void *)0, 9);
}

static void nested_demo_cb(void *arg) {
    (void)arg;
    uint64_t start = trap_uptime_seconds();
    uint64_t end = start + 6;
    uint64_t last = (uint64_t)-1;
    printf("[NestedDemo] start LOW task (prio=1) at %d sec\r\n", (int)start);
    printf("[NestedDemo] arm +1s timer, expect TIMER+HI insert while LOW runs\r\n");
    if (add_timer(nested_timer_kick_cb, (void *)0, 1) < 0) {
        printf("[NestedDemo] failed to arm demo timer\r\n");
    }
    while (trap_uptime_seconds() < end) {
        uint64_t now = trap_uptime_seconds();
        if (now != last) {
            printf("[NestedDemo][LOW] running... now=%d\r\n", (int)now);
            last = now;
        }
        for (volatile unsigned long i = 0; i < 200000UL; i++) {
            asm volatile("" ::: "memory");
        }
    }
    printf("[NestedDemo][LOW] done at %d sec\r\n", (int)trap_uptime_seconds());
}

void start_kernel(uint64_t hart_id, void *dtb_base) {
    // Parse DTB to get UART base address and initialize UART
    // Try both paths: OrangePi RV2 uses "/soc/serial", QEMU uses "/soc/uart"
    int offset = fdt_path_offset(dtb_base, "/soc/serial");
    if (offset < 0) {
        offset = fdt_path_offset(dtb_base, "/soc/uart");
    }
    if (offset >= 0) {
        int len;
        const void *reg = fdt_getprop(dtb_base, offset, "reg", &len);
        if (reg && len >= 8) {
            const uint32_t *cells = (const uint32_t *)reg;
            uint64_t uart_addr =
                ((uint64_t)bswap32(cells[0]) << 32) | bswap32(cells[1]);
            uart_init(uart_addr);
        }
    }

    // Parse initrd start and end addresses from DTB
    offset = fdt_path_offset(dtb_base, "/chosen");
    if (offset >= 0) {
        int len;
        const void *reg =
            fdt_getprop(dtb_base, offset, "linux,initrd-start", &len);
        if (reg) {
            if (len >= 8) {
                g_initrd_start = bswap64(*(const uint64_t *)reg);
            } else if (len >= 4) {
                g_initrd_start = bswap32(*(const uint32_t *)reg);
            }
        }
        reg = fdt_getprop(dtb_base, offset, "linux,initrd-end", &len);
        if (reg) {
            if (len >= 8) {
                g_initrd_end = bswap64(*(const uint64_t *)reg);
            } else if (len >= 4) {
                g_initrd_end = bswap32(*(const uint32_t *)reg);
            }
        }
    }

    uart_puts("\n========================================\n");
    uart_puts("Starting kernel ...\n");
    uart_puts("========================================\n");
    printf("DTB: 0x%x (size: %d bytes)\n", (unsigned long)dtb_base,
           fdt_totalsize(dtb_base));
    printf("Kernel: 0x%x - 0x%x\n", (unsigned long)_kernel_start,
           (unsigned long)_kernel_end);
    if (g_initrd_start && g_initrd_end) {
        printf("Initrd: 0x%x - 0x%x\n", g_initrd_start, g_initrd_end);
    }
    uart_puts("========================================\n\n");

    /* Initialize memory system using startup allocator */
    startup_memory_init(dtb_base, g_initrd_start, g_initrd_end);
    uint64_t timebase = 0;
    if (fdt_get_timebase_frequency(dtb_base, &timebase) == 0) {
        printf("Timebase frequency: %d Hz\r\n", (unsigned int)timebase);
    } else {
        timebase = TIMER_TICK_HZ;
        printf("Timebase frequency: fallback %d Hz\r\n", (unsigned int)timebase);
    }
    trap_init(hart_id, timebase);

#define MAX_CMD_LEN 128
    char cmd_buf[MAX_CMD_LEN];
    int cmd_idx = 0;
    int last_rx_overflow = 0;

    while (1) {
        int cur_overflow = uart_rx_overflow_count();
        if (cur_overflow != last_rx_overflow) {
            printf("\r\n[Warn] UART RX overflow +%d (total=%d)\r\n",
                   cur_overflow - last_rx_overflow, cur_overflow);
            last_rx_overflow = cur_overflow;
        }
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
                printf(
                    "Available commands:\r\n"
                    "  help       - show all commands.\r\n"
                    "  hello      - print hello world.\r\n"
                    "  info       - print system info.\r\n"
                    "  meminfo    - show memory status.\r\n"
                    "  load       - load kernel via UART.\r\n"
                    "  alloc_test - run spec test case (test_alloc_1).\r\n"
                    "  exec [file]- run user program in initrd.\r\n"
                    "  setTimeout <sec> <msg> - delayed non-blocking print.\r\n"
                    "  task_test  - enqueue task callbacks.\r\n"
                    "  nested_test- demo nested interrupt + task preemption.\r\n"
                    "  close_timer- hide periodic 2s timer log output.\r\n"
                    "  open_timer - show periodic 2s timer log output.\r\n"
                    "  ls         - list files in initrd.\r\n"
                    "  cat <file> - display file content.\r\n");
            } else if (strcmp(cmd_buf, "hello") == 0) {
                printf("Hello world.\r\n");
            } else if (strcmp(cmd_buf, "info") == 0) {
                printf("System information:\r\n");
                printf("  OpenSBI specification version: %x\r\n",
                       sbi_get_spec_version());
                printf("  implementation ID: %x\r\n", sbi_get_impl_id());
                printf("  implementation version: %x\r\n",
                       sbi_get_impl_version());
            } else if (strcmp(cmd_buf, "meminfo") == 0) {
                buddy_dump();
            } else if (strcmp(cmd_buf, "load") == 0) {
                load_kernel(dtb_base);
            } else if (strcmp(cmd_buf, "alloc_test") == 0) {
                alloc_test();
            } else if (strcmp(cmd_buf, "task_test") == 0) {
                printf("task_test queued (run on next trap)\r\n");
                add_task(task_test_cb, (void *)"3", 3);
                add_task(task_test_cb, (void *)"1", 1);
                add_task(task_test_cb, (void *)"5", 5);
            } else if (strcmp(cmd_buf, "nested_test") == 0) {
                printf("nested_test queued (LOW task + TIMER + HI preemption)\r\n");
                add_task(nested_demo_cb, (void *)0, 1);
            } else if (strcmp(cmd_buf, "close_timer") == 0) {
                timer_set_periodic_log_enabled(0);
                printf("timer periodic log: OFF\r\n");
            } else if (strcmp(cmd_buf, "open_timer") == 0) {
                timer_set_periodic_log_enabled(1);
                printf("timer periodic log: ON\r\n");
            } else if (strncmp(cmd_buf, "setTimeout ", 11) == 0) {
                const char *p = cmd_buf + 11;
                int i = 0;
                while (p[i] != '\0' && p[i] != ' ') {
                    i++;
                }
                if (i == 0 || p[i] == '\0') {
                    printf("Usage: setTimeout <sec> <msg>\r\n");
                } else {
                    char sec_buf[12];
                    if (i >= (int)sizeof(sec_buf)) {
                        printf("setTimeout: invalid sec\r\n");
                        continue;
                    }
                    strncpy(sec_buf, p, (size_t)i);
                    sec_buf[i] = '\0';
                    int sec = 0;
                    if (parse_dec(sec_buf, &sec) < 0) {
                        printf("setTimeout: invalid sec\r\n");
                        continue;
                    }
                    const char *msg = p + i + 1;
                    if (*msg == '\0') {
                        printf("setTimeout: empty message\r\n");
                        continue;
                    }
                    struct timeout_msg *t = (struct timeout_msg *)allocate(
                        sizeof(struct timeout_msg));
                    if (t == (void *)0) {
                        printf("setTimeout: no memory\r\n");
                        continue;
                    }
                    t->command_time = trap_uptime_seconds();
                    t->sec = sec;
                    strncpy(t->msg, msg, sizeof(t->msg) - 1);
                    t->msg[sizeof(t->msg) - 1] = '\0';
                    if (add_timer(timeout_cb, t, sec) < 0) {
                        free(t);
                        printf("setTimeout: queue full\r\n");
                    }
                }
            } else if (strcmp(cmd_buf, "exec") == 0 ||
                       strncmp(cmd_buf, "exec ", 5) == 0) {
                const char *filename = "prog.bin";
                if (strncmp(cmd_buf, "exec ", 5) == 0 && cmd_buf[5] != '\0') {
                    filename = cmd_buf + 5;
                }
                if (g_initrd_start && g_initrd_end) {
                    size_t fsize = 0;
                    const void *entry = initrd_find_file((void *)g_initrd_start,
                                                         (void *)g_initrd_end,
                                                         filename, &fsize);
                    if (!entry) {
                        printf("exec: %s not found\r\n", filename);
                    } else if (trap_exec_user(entry, fsize) < 0) {
                        printf("exec: failed to enter user mode\r\n");
                    }
                } else {
                    printf("No initrd loaded.\r\n");
                }
            } else if (strcmp(cmd_buf, "ls") == 0) {
                if (g_initrd_start && g_initrd_end) {
                    initrd_list((void *)g_initrd_start, (void *)g_initrd_end);
                } else {
                    printf("No initrd loaded.\r\n");
                }
            } else if (strncmp(cmd_buf, "cat ", 4) == 0) {
                if (g_initrd_start && g_initrd_end) {
                    const char *filename = cmd_buf + 4;
                    initrd_cat((void *)g_initrd_start, (void *)g_initrd_end,
                               filename);
                } else {
                    printf("No initrd loaded.\r\n");
                }
            } else {
                printf("Unknown command: %s\r\n", cmd_buf);
                printf("Use help to get commands\r\n");
            }
        }
    }
}
