extern char uart_getc(void);
extern void uart_putc(char c);
extern void uart_puts(const char* s);
extern void uart_hex(unsigned long h);

#define SBI_EXT_BASE      0x10

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

struct sbiret sbi_ecall(int ext,
                        int fid,
                        unsigned long arg0,
                        unsigned long arg1,
                        unsigned long arg2,
                        unsigned long arg3,
                        unsigned long arg4,
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
    // TODO: Implement this function
    struct sbiret result = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_SPEC_VERSION, 0, 0, 0, 0, 0, 0);
    if (result.error) {
        return result.error;
    }
    return result.value;
}

long sbi_get_impl_id() {
    // TODO: Implement this function
    struct sbiret result = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_ID, 0, 0, 0, 0, 0, 0);
    if (result.error) {
        return result.error;
    }
    return result.value;
}

long sbi_get_impl_version() {
    // TODO: Implement this function
    struct sbiret result = sbi_ecall(SBI_EXT_BASE, SBI_EXT_BASE_GET_IMP_VERSION, 0, 0, 0, 0, 0, 0);
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
            } else if (strcmp(cmd_buf, "info") == 0){
                uart_puts("System information:\n");
                uart_puts("  OpenSBI specification version:");
                uart_hex(sbi_get_spec_version());
                uart_puts("\r\n");
                uart_puts("  implementation ID:");
                uart_hex(sbi_get_impl_id());
                uart_puts("\r\n");
                uart_puts("  implementation version:");
                uart_hex(sbi_get_impl_version());
                uart_puts("\r\n");
            } else {
                uart_puts("Unknown command: ");
                uart_puts(cmd_buf);
                uart_puts("\r\n");
                uart_puts("Use help to get commands\n");
            }
        }
    }
}
