#define LSR_DR (1 << 0)
#define LSR_TDRQ (1 << 5)

static unsigned long uart_base = 0x10000000UL; // Default value

void uart_init(unsigned long base) { uart_base = base; }

static inline volatile unsigned char *uart_rbr(void) {
    return (volatile unsigned char *)(uart_base + 0x0);
}

static inline volatile unsigned char *uart_thr(void) {
    return (volatile unsigned char *)(uart_base + 0x0);
}

static inline volatile unsigned char *uart_lsr(void) {
    return (volatile unsigned char *)(uart_base + 0x5);
}

char uart_getc() {
    while ((*uart_lsr() & LSR_DR) == 0)
        ;
    char c = (char)*uart_rbr();
    return c == '\r' ? '\n' : c;
}

// Raw version without CR->LF conversion (for binary data)
char uart_getc_raw() {
    while ((*uart_lsr() & LSR_DR) == 0)
        ;
    return (char)*uart_rbr();
}

void uart_putc(char c) {
    if (c == '\n')
        uart_putc('\r');

    while ((*uart_lsr() & LSR_TDRQ) == 0)
        ;
    *uart_thr() = c;
}

void uart_puts(const char *s) {
    while (*s)
        uart_putc(*s++);
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
