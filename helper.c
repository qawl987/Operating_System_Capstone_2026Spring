#include "helper.h"
#include <stdarg.h>

// UART functions (defined in uart.c)
extern void uart_putc(char c);
extern void uart_puts(const char *s);
extern void uart_hex(unsigned long h);

size_t strlen(const char *s) {
    size_t len = 0;
    while (*s++)
        len++;
    return len;
}

int strcmp(const char *s1, const char *s2) {
    while (*s1 != '\0' && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

int strncmp(const char *s1, const char *s2, size_t n) {
    while (n > 0 && *s1 != '\0' && *s1 == *s2) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0)
        return 0;
    return *(const unsigned char *)s1 - *(const unsigned char *)s2;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n > 0 && *src != '\0') {
        *d++ = *src++;
        n--;
    }
    while (n > 0) {
        *d++ = '\0';
        n--;
    }
    return dest;
}

uint32_t bswap32(uint32_t x) {
    return ((x & 0x000000FFU) << 24) | ((x & 0x0000FF00U) << 8) |
           ((x & 0x00FF0000U) >> 8) | ((x & 0xFF000000U) >> 24);
}

uint64_t bswap64(uint64_t x) {
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8) |
           ((x & 0x000000FF00000000ULL) >> 8) |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
}

const void *align_up(const void *ptr, size_t align) {
    return (const void *)(((uintptr_t)ptr + align - 1) & ~(align - 1));
}

static void uart_dec(int num) {
    if (num == 0) {
        uart_putc('0');
        return;
    }

    unsigned int unum;
    if (num < 0) {
        uart_putc('-');
        unum = (unsigned int)(-num);
    } else {
        unum = (unsigned int)num;
    }

    char buf[16];
    int i = 0;

    while (unum > 0) {
        buf[i++] = (unum % 10) + '0';
        unum /= 10;
    }

    while (i > 0) {
        i--;
        uart_putc(buf[i]);
    }
}

void printf(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    while (*fmt != '\0') {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
            case 's': {
                char *s = va_arg(args, char *);
                uart_puts(s);
                break;
            }
            case 'c': {
                char c = (char)va_arg(args, int);
                uart_putc(c);
                break;
            }
            case 'x': {
                unsigned long x = va_arg(args, unsigned long);
                uart_hex(x);
                break;
            }
            case 'd': {
                int d = va_arg(args, int);
                uart_dec(d);
                break;
            }
            case '%': {
                uart_putc('%');
                break;
            }
            default: {
                uart_putc('%');
                uart_putc(*fmt);
                break;
            }
            }
        } else {
            uart_putc(*fmt);
        }
        fmt++;
    }

    va_end(args);
}
