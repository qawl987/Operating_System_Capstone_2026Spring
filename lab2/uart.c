#ifdef PLATFORM_PI
// Orange Pi RV2: PXA UART, reg-shift=2, reg-io-width=4
#define UART_BASE 0xD4017000UL
#define REG_SHIFT 2
#define LSR_DR (1 << 0)
#define LSR_TDRQ (1 << 6) // TEMT for PXA
typedef volatile unsigned int uart_reg_t;
#else
// QEMU virt: 16550 UART, 8-bit registers
#define UART_BASE 0x10000000UL
#define REG_SHIFT 0
#define LSR_DR (1 << 0)
#define LSR_TDRQ (1 << 5) // THRE for 16550
typedef volatile unsigned char uart_reg_t;
#endif

static unsigned long uart_base = UART_BASE;

void uart_init(unsigned long base) { uart_base = base; }

static inline uart_reg_t *uart_rbr(void) {
    return (uart_reg_t *)(uart_base + (0x0 << REG_SHIFT));
}

static inline uart_reg_t *uart_thr(void) {
    return (uart_reg_t *)(uart_base + (0x0 << REG_SHIFT));
}

static inline uart_reg_t *uart_lsr(void) {
    return (uart_reg_t *)(uart_base + (0x5 << REG_SHIFT));
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
