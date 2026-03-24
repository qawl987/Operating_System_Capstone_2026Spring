#ifdef PLATFORM_PI
// Orange Pi RV2: PXA UART, reg-shift=2, reg-io-width=4
#define UART_BASE   0xD4017000UL
#define REG_SHIFT   2
#define LSR_DR      (1 << 0)
#define LSR_TDRQ    (1 << 6)  // TEMT for PXA
typedef volatile unsigned int uart_reg_t;
#else
// QEMU virt: 16550 UART, 8-bit registers
#define UART_BASE   0x10000000UL
#define REG_SHIFT   0
#define LSR_DR      (1 << 0)
#define LSR_TDRQ    (1 << 5)  // THRE for 16550
typedef volatile unsigned char uart_reg_t;
#endif

#define UART_RBR ((uart_reg_t *)(UART_BASE + (0x0 << REG_SHIFT)))
#define UART_THR ((uart_reg_t *)(UART_BASE + (0x0 << REG_SHIFT)))
#define UART_LSR ((uart_reg_t *)(UART_BASE + (0x5 << REG_SHIFT)))

char uart_getc(void) {
    while (!(*UART_LSR & LSR_DR))
        ;
    char c = (char)*UART_RBR;
    return c == '\r' ? '\n' : c;
}

void uart_putc(char c) {
    if (c == '\n') {
        while (!(*UART_LSR & LSR_TDRQ))
            ;
        *UART_THR = '\r';
    }
    while (!(*UART_LSR & LSR_TDRQ))
        ;
    *UART_THR = c;
}

void uart_puts(const char *s) {
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