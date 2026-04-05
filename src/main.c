#include "bootloader.h"
#include "buddy.h"
#include "dtbParser.h"
#include "helper.h"
#include "initrd.h"
#include "kmalloc.h"
#include "sbi.h"
#include "uart.h"

/* Linker symbols for kernel boundaries */
extern char _kernel_start[];
extern char _kernel_end[];

/* Global state for initrd addresses */
static unsigned long g_initrd_start = 0;
static unsigned long g_initrd_end = 0;

/**
 * Parse /reserved-memory node and reserve all child regions
 */
static void parse_reserved_memory(void *dtb_base) {
    int parent_offset = fdt_path_offset(dtb_base, "/reserved-memory");
    if (parent_offset < 0) {
        uart_puts("No /reserved-memory node found\n");
        return;
    }

    uart_puts("Parsing /reserved-memory...\n");

    /* Iterate through child nodes */
    int child_offset = fdt_first_subnode(dtb_base, parent_offset);
    while (child_offset >= 0) {
        int len;
        const void *reg = fdt_getprop(dtb_base, child_offset, "reg", &len);
        if (reg && len >= 16) {
            const uint32_t *cells = (const uint32_t *)reg;
            /* reg = <addr_hi addr_lo size_hi size_lo> */
            unsigned long addr = ((uint64_t)bswap32(cells[0]) << 32) | bswap32(cells[1]);
            unsigned long size = ((uint64_t)bswap32(cells[2]) << 32) | bswap32(cells[3]);

            printf("  Reserving region: 0x%x - 0x%x\n", addr, addr + size);
            memory_reserve(addr, size);
        }

        child_offset = fdt_next_subnode(dtb_base, child_offset);
    }
}

/**
 * Initialize memory with reservations from DTB
 * Parses DTB to get memory region and reserves:
 * 1. DTB blob
 * 2. Kernel image
 * 3. Initramfs (if present)
 * 4. Reserved memory regions (if present)
 */
static void memory_init_with_reserve(void *dtb_base,
                                     unsigned long initrd_start,
                                     unsigned long initrd_end) {
    int offset, len;
    const void *prop;
    unsigned long mem_start = 0;
    unsigned long mem_size = 0;

    /* 1. Get memory region from DTB /memory node */
    offset = fdt_path_offset(dtb_base, "/memory");
    if (offset >= 0) {
        prop = fdt_getprop(dtb_base, offset, "reg", &len);
        if (prop && len >= 16) {
            const uint32_t *cells = (const uint32_t *)prop;
            /* reg = <addr_hi addr_lo size_hi size_lo> */
            mem_start = ((uint64_t)bswap32(cells[0]) << 32) | bswap32(cells[1]);
            mem_size = ((uint64_t)bswap32(cells[2]) << 32) | bswap32(cells[3]);
        }
    }

    if (mem_start == 0 || mem_size == 0) {
        uart_puts("[!] Failed to get memory region from DTB, using default\n");
        mem_start = 0x80000000UL;
        mem_size = 0x80000000UL; /* 2GB default */
    }

    printf("Memory region: 0x%x - 0x%x (%d MB)\n",
           mem_start, mem_start + mem_size, mem_size / (1024 * 1024));

    /* Initialize buddy system with the memory region */
    buddy_init(mem_start, mem_size);

    /* 2. Reserve DTB blob */
    unsigned long dtb_start = (unsigned long)dtb_base;
    unsigned long dtb_size = fdt_totalsize(dtb_base);
    printf("Reserving DTB: 0x%x - 0x%x\n", dtb_start, dtb_start + dtb_size);
    memory_reserve(dtb_start, dtb_size);

    /* 3. Reserve Kernel image */
    unsigned long kernel_start = (unsigned long)_kernel_start;
    unsigned long kernel_end = (unsigned long)_kernel_end;
    unsigned long kernel_size = kernel_end - kernel_start;
    printf("Reserving Kernel: 0x%x - 0x%x\n", kernel_start, kernel_end);
    memory_reserve(kernel_start, kernel_size);

    /* 4. Reserve Initramfs (if present) */
    if (initrd_start && initrd_end && initrd_end > initrd_start) {
        printf("Reserving Initramfs: 0x%x - 0x%x\n", initrd_start, initrd_end);
        memory_reserve(initrd_start, initrd_end - initrd_start);
    }

    /* 5. Parse and reserve /reserved-memory regions */
    parse_reserved_memory(dtb_base);

    /* Initialize dynamic allocator */
    kmalloc_init();

    uart_puts("Memory initialization complete.\n");
    buddy_dump();
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
            // reg format depends on #address-cells (typically 2 for 64-bit)
            // Each cell is 32-bit big-endian: [addr_hi, addr_lo, size_hi,
            // size_lo]
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
    printf("DTB: 0x%x (size: %d bytes)\n", (unsigned long)dtb_base, fdt_totalsize(dtb_base));
    printf("Kernel: 0x%x - 0x%x\n", (unsigned long)_kernel_start, (unsigned long)_kernel_end);
    if (g_initrd_start && g_initrd_end) {
        printf("Initrd: 0x%x - 0x%x\n", g_initrd_start, g_initrd_end);
    }
    uart_puts("========================================\n\n");

    /* Initialize memory system with reservations at startup */
    memory_init_with_reserve(dtb_base, g_initrd_start, g_initrd_end);
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
                       "  help       - show all commands.\r\n"
                       "  hello      - print hello world.\r\n"
                       "  info       - print system info.\r\n"
                       "  meminfo    - show memory status.\r\n"
                       "  load       - load kernel via UART.\r\n"
                       "  alloc_test - run spec test case (test_alloc_1).\r\n"
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
            } else if (strcmp(cmd_buf, "ls") == 0) {
                if (g_initrd_start && g_initrd_end) {
                    initrd_list((void *)g_initrd_start,
                                (void *)g_initrd_end);
                } else {
                    printf("No initrd loaded.\r\n");
                }
            } else if (strncmp(cmd_buf, "cat ", 4) == 0) {
                if (g_initrd_start && g_initrd_end) {
                    const char *filename = cmd_buf + 4;
                    initrd_cat((void *)g_initrd_start,
                               (void *)g_initrd_end, filename);
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