#include "bootloader.h"
#include "buddy.h"
#include "dtbParser.h"
#include "helper.h"
#include "initrd.h"
#include "kmalloc.h"
#include "sbi.h"
#include "uart.h"

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
            printf("UART base address: %x\n", uart_addr);
        }
    }

    // Parse initrd start and end addresses from DTB
    unsigned long initrd_start_addr = 0;
    unsigned long initrd_end_addr = 0;
    offset = fdt_path_offset(dtb_base, "/chosen");
    if (offset >= 0) {
        int len;
        const void *reg =
            fdt_getprop(dtb_base, offset, "linux,initrd-start", &len);
        if (reg) {
            if (len >= 8) {
                initrd_start_addr = bswap64(*(const uint64_t *)reg);
            } else if (len >= 4) {
                initrd_start_addr = bswap32(*(const uint32_t *)reg);
            }
        }
        reg = fdt_getprop(dtb_base, offset, "linux,initrd-end", &len);
        if (reg) {
            if (len >= 8) {
                initrd_end_addr = bswap64(*(const uint64_t *)reg);
            } else if (len >= 4) {
                initrd_end_addr = bswap32(*(const uint32_t *)reg);
            }
        }
    }

    uart_puts("\nStarting kernel ...\n");
    printf("DTB: %x\n", (unsigned long)dtb_base);
    if (initrd_start_addr && initrd_end_addr) {
        printf("initrd: %x - %x\n", initrd_start_addr, initrd_end_addr);
    }
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
                       "  help      - show all commands.\r\n"
                       "  hello     - print hello world.\r\n"
                       "  info      - print system info.\r\n"
                       "  load      - load kernel via UART.\r\n"
                       "  buddy     - test buddy system allocator.\r\n"
                       "  kmalloc   - test dynamic memory allocator.\r\n"
                       "  alloc_test- run spec test case (test_alloc_1).\r\n"
                       "  ls        - list files in initrd.\r\n"
                       "  cat <file>- display file content.\r\n");
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
                load_kernel(dtb_base);
            } else if (strcmp(cmd_buf, "buddy") == 0) {
                buddy_test();
            } else if (strcmp(cmd_buf, "kmalloc") == 0) {
                buddy_init(0x90000000UL, 0x10000000UL);
                kmalloc_init();
                kmalloc_test();
            } else if (strcmp(cmd_buf, "alloc_test") == 0) {
                buddy_init(0x90000000UL, 0x10000000UL);
                kmalloc_init();
                alloc_test();
            } else if (strcmp(cmd_buf, "ls") == 0) {
                if (initrd_start_addr && initrd_end_addr) {
                    initrd_list((void *)initrd_start_addr,
                                (void *)initrd_end_addr);
                } else {
                    printf("No initrd loaded.\r\n");
                }
            } else if (strncmp(cmd_buf, "cat ", 4) == 0) {
                if (initrd_start_addr && initrd_end_addr) {
                    const char *filename = cmd_buf + 4;
                    initrd_cat((void *)initrd_start_addr,
                               (void *)initrd_end_addr, filename);
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