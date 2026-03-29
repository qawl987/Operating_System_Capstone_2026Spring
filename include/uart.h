#ifndef UART_H
#define UART_H

void uart_init(unsigned long base);
char uart_getc(void);
char uart_getc_raw(void);
void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(unsigned long h);

#endif /* UART_H */
