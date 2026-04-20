#ifndef UART_H
#define UART_H

void uart_init(unsigned long base);
void uart_enable_rx_interrupt(void);
void uart_enable_tx_interrupt(void);
void uart_disable_tx_interrupt(void);
void uart_handle_irq(void);
int uart_rx_overflow_count(void);
char uart_getc(void);
char uart_getc_raw(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(unsigned long h);
void uart_puti(int i);
void uart_putx(unsigned long h);

#endif /* UART_H */
