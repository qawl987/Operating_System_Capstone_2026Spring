#define UART_BASE 0x10000000UL
#define UART_RBR  ((volatile unsigned char*)(UART_BASE + 0x0))
#define UART_THR  ((volatile unsigned char*)(UART_BASE + 0x0))
#define UART_LSR  ((volatile unsigned char*)(UART_BASE + 0x5))
#define LSR_DR    (1 << 0)
#define LSR_TDRQ  (1 << 5)

char uart_getc() {
    while (!(*UART_LSR & LSR_DR));
    return *UART_RBR;
}

void uart_putc(char c) {
    while (!(*UART_LSR & LSR_TDRQ));
    *UART_THR = c;
}

void uart_puts(const char* s) {
    while (*s) {
        uart_putc(*s++);
    }
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        s1++;
        s2++;
    }
    // 回傳相差的值，若字串一樣，最後相減會是 0
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

void start_kernel() {
    uart_puts("\nStarting kernel ...\n");
    #define MAX_CMD_LEN 128
    char cmd_buf[MAX_CMD_LEN];
    int cmd_idx = 0;

    while (1) {
        // 1. 印出提示字元 (每次新指令只印一次)
        uart_puts("opi-rv2> ");
        cmd_idx = 0; // 重置緩衝區索引

        // 2. 內層迴圈：接收輸入直到按下 Enter
        while (1) {
            char c = uart_getc();

            if (c == '\r' || c == '\n') {
                // 收到 Enter：換行，並在字串結尾補上 '\0' 形成合法 C 字串
                uart_puts("\r\n");
                cmd_buf[cmd_idx] = '\0';
                break; // 跳出內層迴圈，準備處理指令
            } 
            else if (c == '\b' || c == '\x7f') {
                // 收到退格鍵 (Backspace 或 Delete)
                if (cmd_idx > 0) {
                    cmd_idx--;
                    // 終端機游標退後一格，印出空白蓋掉原本的字，再退後一格
                    uart_puts("\b \b"); 
                }
            } 
            else if (cmd_idx < MAX_CMD_LEN - 1) {
                // 其他正常字元：存入緩衝區並回顯 (Echo)
                cmd_buf[cmd_idx++] = c;
                uart_putc(c);
            }
        }

        // 3. 指令處理階段 (跳出內層迴圈後)
        if (cmd_idx > 0) {
            // 這裡可以檢查 cmd_buf 裡面的字串
            if (strcmp(cmd_buf, "help") == 0) {
                uart_puts("Available commands:\n");
                uart_puts("  help  - show all commands.\n");
                uart_puts("  hello - print hello world.\n");
                uart_puts("  info  - print system info.\n");
            } else if (strcmp(cmd_buf, "hello world") == 0){
                uart_puts("Hello world.\n");
            }  else {
                uart_puts("Unknown command: ");
                uart_puts(cmd_buf);
                uart_puts("\r\n");
                uart_puts("Use help to get commands\n");
            }
        }
    }
}
