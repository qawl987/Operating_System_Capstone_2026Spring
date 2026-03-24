#include "config.h"
#include "helper.h"
#include "uart.h"
#include <stdint.h>

// 在 start.S 中定義的重定位函式
extern void relocate_and_continue(void *dtb, void (*continue_func)(void *dtb));

// 計算函式在高位的地址
#define HIGH_ADDR(func)                                                        \
    ((void (*)(void *))(RELOC_ADDR + ((unsigned long)(func) - LOAD_ADDR)))

// 接收並載入 kernel 的實際工作函式
// 此函式會在重定位後從高位被呼叫
static void do_load_kernel(void *dtb) {
    uart_puts("Waiting for kernel image via UART...\r\n");

    uint32_t magic = 0;
    uint32_t size = 0;

    // 1. 讀取 4 bytes 的 Magic Number
    unsigned char *magic_ptr = (unsigned char *)&magic;
    for (int i = 0; i < 4; i++) {
        magic_ptr[i] = uart_getc_raw();
    }

    // 檢查 Magic Number 是否正確
    if (magic != BOOT_MAGIC) {
        printf("Error: Invalid magic number: %x\r\n", magic);
        return;
    }

    // 2. 讀取 4 bytes 的 Kernel Size
    unsigned char *size_ptr = (unsigned char *)&size;
    for (int i = 0; i < 4; i++) {
        size_ptr[i] = uart_getc_raw();
    }

    printf("Magic matched! Kernel size: %d bytes\r\n", size);
    printf("Loading kernel to: %x\r\n", LOAD_ADDR);

    // 3. 讀取 Kernel 資料並寫入低位記憶體
    unsigned char *kernel_mem = (unsigned char *)LOAD_ADDR;

    // 進度條設定
    const int bar_width = 50;
    int last_percent = -1;

    uart_puts("[");
    for (int i = 0; i < bar_width; i++)
        uart_putc(' ');
    uart_puts("] 0%");

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

    uart_puts("\r\n");
    printf("Jumping to kernel at %x (DTB: %x)\r\n", LOAD_ADDR,
           (unsigned long)dtb);

    // 4. 跳轉到新 kernel
    register unsigned long a0_hart asm("a0") = 0;
    register unsigned long a1_dtb asm("a1") = (unsigned long)dtb;
    void (*kernel_entry)(void) = (void (*)(void))LOAD_ADDR;
    asm volatile("" : : "r"(a0_hart), "r"(a1_dtb));
    kernel_entry();
}

// load 指令的入口點
// 先把 bootloader 搬到高位，再從高位執行 do_load_kernel
void load_kernel(void *dtb) {
    uart_puts("Relocating bootloader to high memory...\r\n");

    // 計算 do_load_kernel 在高位的地址
    void (*high_func)(void *) = HIGH_ADDR(do_load_kernel);

    // 搬移並跳轉到高位繼續執行
    // 此函式不會返回
    relocate_and_continue(dtb, high_func);
}