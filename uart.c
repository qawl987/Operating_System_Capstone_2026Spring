struct sbiret {
    long error;
    long value;
};
extern struct sbiret sbi_ecall(int ext, int fid, unsigned long arg0, unsigned long arg1, 
                               unsigned long arg2, unsigned long arg3, unsigned long arg4, unsigned long arg5);

// 呼叫 OpenSBI Legacy Console Putchar (Extension ID: 1)
void uart_putc(char c) {
    if (c == '\n') {
        sbi_ecall(1, 0, '\r', 0, 0, 0, 0, 0); // 處理換行對齊
    }
    sbi_ecall(1, 0, c, 0, 0, 0, 0, 0);
}

// 呼叫 OpenSBI Legacy Console Getchar (Extension ID: 2)
char uart_getc(void) {
    struct sbiret ret;
    do {
        ret = sbi_ecall(2, 0, 0, 0, 0, 0, 0, 0);
    } while (ret.error == -1); // 如果回傳 -1 代表還沒有按下鍵盤，繼續等
    
    char c = (char)ret.error;
    return c == '\r' ? '\n' : c;
}

void uart_puts(const char* s) {
    while (*s) {
        uart_putc(*s++);
    }
}

void uart_hex(unsigned long h) {
    uart_puts("0x");
    unsigned long n;
    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        n += n > 9 ? 0x57 : '0';
        uart_putc(n);
    }
}