#include "config.h"
#include <stdint.h>

// 宣告在其他檔案實作的 UART 函式
extern char uart_getc_raw(void);
extern void uart_puts(const char *s);
extern void uart_putc(char c);
extern void printf(const char *fmt, ...);

// KERNEL_LOAD_ADDR 從 config.h 取得 (LOAD_ADDR)
// 由於 bootloader 已自我重定位，可以載入到標準入口點
#define KERNEL_LOAD_ADDR LOAD_ADDR

// 讀取 RISC-V time CSR (需要 SBI 或直接讀取 mtime)
// static inline uint64_t read_time(void) {
//     uint64_t time;
//     asm volatile("rdtime %0" : "=r"(time));
//     return time;
// }

void load_kernel(void *dtb) {
    uart_puts("Waiting for kernel image via UART...\r\n");

    uint32_t magic = 0;
    uint32_t size = 0;

    // 1. 讀取 4 bytes 的 Magic Number
    // Python 端用 '<II' 封裝，代表是 Little-Endian
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
    printf("Loading kernel to memory address: %x\r\n", KERNEL_LOAD_ADDR);

    // 3. 讀取 Kernel 資料並寫入指定的記憶體位址
    unsigned char *kernel_mem = (unsigned char *)KERNEL_LOAD_ADDR;

    // 進度條設定
    const int bar_width = 50; // 進度條寬度
    int last_percent = -1;

    // 開始計時
    // uint64_t start_time = read_time();

    uart_puts("[");
    for (int i = 0; i < bar_width; i++)
        uart_putc(' ');
    uart_puts("] 0%");

    for (uint32_t i = 0; i < size; i++) {
        kernel_mem[i] = uart_getc_raw();

        // 計算當前進度百分比
        int percent = (int)(((uint64_t)(i + 1) * 100) / size);

        // 每 10% 更新一次進度條，減少 UART 輸出量
        if (percent != last_percent && percent % 10 == 0) {
            last_percent = percent;
            int filled = (percent * bar_width) / 100;

            // 回到行首重繪進度條
            uart_putc('\r');
            uart_putc('[');
            for (int j = 0; j < bar_width; j++) {
                if (j < filled)
                    uart_putc('=');
                else
                    uart_putc(' ');
            }
            printf("] %d%%", percent);
        }
    }

    // 結束計時並顯示時間
    // uint64_t end_time = read_time();
    // uint64_t elapsed = end_time - start_time;
    // QEMU timer frequency is typically 10MHz, OrangePi may differ
    // 顯示原始 ticks 和估算時間 (假設 10MHz)
    // uint64_t ms = elapsed / 10000; // 10MHz -> ms
    // printf("\r\nKernel loaded successfully! (Time: %d ms, %d ticks)\r\n",
    //        (uint32_t)ms, (uint32_t)elapsed);
    uart_puts("Jumping to kernel...\r\n");

    // 4. 交出控制權：跳轉到 Kernel 載入的位址
    // 新 kernel 的 _start 期望 DTB 地址在 a1 寄存器中
    register unsigned long a1_dtb asm("a1") = (unsigned long)dtb;
    void (*kernel_entry)(void) = (void (*)(void))KERNEL_LOAD_ADDR;
    asm volatile("" : : "r"(a1_dtb)); // 確保 a1 被設置
    kernel_entry();
}