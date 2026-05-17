#include "config.h"
#include "helper.h"
#include "trap.h"
#include "uart.h"
#include <stdint.h>

// Relocation function defined in start.S
extern void relocate_and_continue(void *dtb, void (*continue_func)(void *dtb));

// Calculate function address in high memory
#define HIGH_ADDR(func)                                                        \
    ((void (*)(void *))(RELOC_ADDR + ((unsigned long)(func) - LOAD_ADDR)))

// Actual kernel loading function
// This function is called from high memory after relocation
static void do_load_kernel(void *dtb) {
    uart_puts_boot("Waiting for kernel image via UART...\r\n");

    uint32_t magic = 0;
    uint32_t size = 0;

    // 1. Read 4 bytes magic number
    unsigned char *magic_ptr = (unsigned char *)&magic;
    for (int i = 0; i < 4; i++) {
        magic_ptr[i] = uart_getc_raw();
    }

    // Verify magic number
    if (magic != BOOT_MAGIC) {
        printf("Error: Invalid magic number: %x\r\n", magic);
        return;
    }

    // 2. Read 4 bytes kernel size
    unsigned char *size_ptr = (unsigned char *)&size;
    for (int i = 0; i < 4; i++) {
        size_ptr[i] = uart_getc_raw();
    }

    printf("Magic matched! Kernel size: %d bytes\r\n", size);
    printf("Loading kernel to: %x\r\n", LOAD_ADDR);

    // 3. Read kernel data and write to low memory
    unsigned char *kernel_mem = (unsigned char *)LOAD_ADDR;

    // Progress bar setup
    const int bar_width = 50;
    int last_percent = -1;

    uart_puts_boot("[");
    for (int i = 0; i < bar_width; i++)
        uart_putc(' ');
    uart_puts_boot("] 0%");

    for (uint32_t i = 0; i < size; i++) {
        kernel_mem[i] = uart_getc_raw();

        int percent = (int)(((uint64_t)(i + 1) * 100) / size);

        if (percent != last_percent && percent % 10 == 0) {
            last_percent = percent;
            int filled = (percent * bar_width) / 100;

            uart_putc('\r');
            uart_putc('[');
            for (int j = 0; j < bar_width; j++) {
                uart_putc(j < filled ? '=' : ' ');
            }
            printf("] %d%%", percent);
        }
    }

    uart_puts_boot("\r\n");
    printf("Jumping to kernel at %x (DTB: %x)\r\n", LOAD_ADDR,
           (unsigned long)dtb);

    // 4. Jump to new kernel
    register unsigned long a0_hart asm("a0") = 0;
    register unsigned long a1_dtb asm("a1") = (unsigned long)dtb;
    void (*kernel_entry)(void) = (void (*)(void))LOAD_ADDR;
    asm volatile("" : : "r"(a0_hart), "r"(a1_dtb));
    kernel_entry();
}

// Entry point for load command
// First relocate bootloader to high memory, then execute do_load_kernel from there
void load_kernel(void *dtb) {
    trap_enter_loader_mode();
    uart_enter_polling_mode();
    uart_puts_boot("Relocating bootloader to high memory...\r\n");

    // Calculate do_load_kernel address in high memory
    void (*high_func)(void *) = HIGH_ADDR(do_load_kernel);

    // Relocate and jump to high memory to continue execution
    // This function does not return
    relocate_and_continue(dtb, high_func);
}
