#include "config.h"
#include <stdint.h>

// 宣告在其他檔案實作的 UART 函式
extern char uart_getc_raw(void);
extern void uart_puts(const char *s);
extern void uart_putc(char c);
extern void printf(const char *fmt, ...);

// KERNEL_LOAD_ADDR 從 config.h 取得 (LOAD_ADDR)
#define KERNEL_LOAD_ADDR LOAD_ADDR

// Trampoline 代碼放在安全的高位地址 (不會被新 kernel 覆蓋)
// 這個地址在 initrd 之前，且遠離 0x20000000 的 bootloader
#define TRAMPOLINE_ADDR 0x40000000ULL

// Trampoline 機器碼: 跳轉到 a2 指定的地址
// 指令: jr a2 (jalr x0, 0(a2)) = 0x00060067
static const uint32_t trampoline_code[] = {
    0x00060067, // jr a2
};

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

    uart_puts("\r\n");
    printf("Jumping to kernel at %x (DTB: %x)\r\n", KERNEL_LOAD_ADDR,
           (unsigned long)dtb);

    // 4. 使用 trampoline 安全地跳轉到新 kernel
    // 問題：當前 bootloader 在 0x20000000 運行，新 kernel
    // 會重定位到同一位置並覆蓋它 解決：先跳到安全位置的
    // trampoline，再從那裡跳到新 kernel

    // 複製 trampoline 代碼到安全位置
    volatile uint32_t *tramp_dest = (volatile uint32_t *)TRAMPOLINE_ADDR;
    for (unsigned i = 0; i < sizeof(trampoline_code) / sizeof(uint32_t); i++) {
        tramp_dest[i] = trampoline_code[i];
    }

    // 確保 instruction cache 同步 (fence.i)
    // 使用 .insn 來編碼 fence.i 指令，避免需要 zifencei 擴展
    asm volatile(".insn i 0x0F, 0x1, x0, x0, 0x000");

    // 跳轉到 trampoline，由它跳到新 kernel
    // a0 = hart_id, a1 = dtb, a2 = kernel entry point
    register unsigned long a0_hart asm("a0") = 0;
    register unsigned long a1_dtb asm("a1") = (unsigned long)dtb;
    register unsigned long a2_entry asm("a2") = KERNEL_LOAD_ADDR;

    void (*trampoline)(void) = (void (*)(void))TRAMPOLINE_ADDR;
    asm volatile("" : : "r"(a0_hart), "r"(a1_dtb), "r"(a2_entry));
    trampoline();
}