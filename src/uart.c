#ifdef PLATFORM_PI
// Orange Pi RV2: PXA UART, reg-shift=2, reg-io-width=4
#define UART_BASE 0xD4017000UL
#define REG_SHIFT 2
#define LSR_DR (1 << 0)
#define LSR_TDRQ (1 << 6) // TEMT for PXA
typedef volatile unsigned int uart_reg_t;
#define IER_RX_ENABLE (1 << 0)
#define IER_TX_ENABLE (1 << 1)
#else
// QEMU virt: 16550 UART, 8-bit registers
#define UART_BASE 0x10000000UL
#define REG_SHIFT 0
#define LSR_DR (1 << 0)
#define LSR_TDRQ (1 << 5) // THRE for 16550
typedef volatile unsigned char uart_reg_t;
#define IER_RX_ENABLE (1 << 0)
#define IER_TX_ENABLE (1 << 1)
#endif

static unsigned long uart_base = UART_BASE;
#define UART_BUF_SIZE 256
static char rx_buf[UART_BUF_SIZE];
static char tx_buf[UART_BUF_SIZE];
static volatile unsigned int rx_r;
static volatile unsigned int rx_w;
static volatile unsigned int tx_r;
static volatile unsigned int tx_w;
static volatile int rx_overflow;

static inline unsigned long irq_save(void) {
    unsigned long s;
    asm volatile("csrr %0, sstatus" : "=r"(s));
    asm volatile("csrci sstatus, 2");
    return s;
}

static inline void irq_restore(unsigned long s) {
    if (s & 2UL) {
        asm volatile("csrsi sstatus, 2");
    } else {
        asm volatile("csrci sstatus, 2");
    }
}

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

static inline uart_reg_t *uart_ier(void) {
    return (uart_reg_t *)(uart_base + (0x1 << REG_SHIFT));
}

static inline unsigned int next_idx(unsigned int idx) {
    return (idx + 1U) % UART_BUF_SIZE;
}

void uart_enable_rx_interrupt(void) { *uart_ier() |= IER_RX_ENABLE; }

void uart_enable_tx_interrupt(void) { *uart_ier() |= IER_TX_ENABLE; }

void uart_disable_tx_interrupt(void) { *uart_ier() &= ~IER_TX_ENABLE; }

void uart_handle_irq(void) {
    unsigned long irq_state = irq_save();
    /* --- PART 1: RECEIVE LOGIC (RX) --- */
    // Check if there is data ready in the hardware FIFO/Register
    // LSR_DR: Line Status Register - Data Ready
    while ((*uart_lsr() & LSR_DR) != 0) {
        unsigned int n = next_idx(rx_w); // Calculate next write position
        char c =
            (char)*uart_rbr(); // Read byte from hardware (clears RX interrupt)

        // If software ring buffer is NOT full, store the character
        if (n != rx_r) {
            rx_buf[rx_w] = c; // Put incoming byte in software buffer
            rx_w = n;         // Update write index
        } else {
            rx_overflow++;
        }
        // If buffer is full, the character 'c' is unfortunately dropped
    }

    /* --- PART 2: TRANSMIT LOGIC (TX) --- */
    // Check if the hardware transmitter is empty and ready for more data
    // LSR_TDRQ: Line Status Register - Transmit Data Request (or Empty)
    if ((*uart_lsr() & LSR_TDRQ) != 0) {
        // If there is data waiting in our software TX buffer
        if (tx_r != tx_w) {
            // Move one byte from software buffer to hardware
            *uart_thr() = tx_buf[tx_r];
            tx_r = next_idx(tx_r); // Update read index
        } else {
            // Buffer is empty! Disable TX interrupts to stop the hardware
            // from constantly asking for more data.
            uart_disable_tx_interrupt();
        }
    }
    irq_restore(irq_state);
}

char uart_getc() {
    while (1) {
        unsigned long irq_state = irq_save();
        if (rx_r != rx_w) {
            char c = rx_buf[rx_r];
            rx_r = next_idx(rx_r);
            irq_restore(irq_state);
            return c == '\r' ? '\n' : c;
        }
        irq_restore(irq_state);
        if ((*uart_lsr() & LSR_DR) != 0) {
            uart_handle_irq();
        }
    }
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

    while (1) {
        unsigned long irq_state = irq_save();
        unsigned int n = next_idx(tx_w);
        if (n != tx_r) {
            tx_buf[tx_w] = c;
            tx_w = n;
            uart_enable_tx_interrupt();
            int tx_ready = ((*uart_lsr() & LSR_TDRQ) != 0);
            irq_restore(irq_state);
            if (tx_ready) {
                uart_handle_irq();
            }
            return;
        }
        irq_restore(irq_state);
        if ((*uart_lsr() & LSR_TDRQ) != 0) {
            uart_handle_irq();
        }
    }
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

void uart_puti(int i) {
    char buf[12];
    int idx = 0;
    unsigned int u;

    if (i < 0) {
        uart_putc('-');
        u = -i;
    } else {
        u = i;
    }

    if (u == 0) {
        uart_putc('0');
        return;
    }

    while (u > 0) {
        buf[idx++] = '0' + (u % 10);
        u /= 10;
    }

    while (idx > 0) {
        uart_putc(buf[--idx]);
    }
}

void uart_putx(unsigned long h) {
    unsigned long n;
    int started = 0;

    for (int c = 60; c >= 0; c -= 4) {
        n = (h >> c) & 0xf;
        if (n || started || c == 0) {
            started = 1;
            n += n > 9 ? 0x57 : '0';
            uart_putc(n);
        }
    }
}

int uart_rx_overflow_count(void) { return rx_overflow; }
