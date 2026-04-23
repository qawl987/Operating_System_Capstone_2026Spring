#ifndef HELPER_H
#define HELPER_H

#include <stddef.h>
#include <stdint.h>

// String functions
size_t strlen(const char *s);
int strcmp(const char *s1, const char *s2);
int strncmp(const char *s1, const char *s2, size_t n);
char *strncpy(char *dest, const char *src, size_t n);

// Byte swap functions
uint32_t bswap32(uint32_t x);
uint64_t bswap64(uint64_t x);

// Alignment helper
const void *align_up(const void *ptr, size_t align);
size_t align_up_val(size_t val, size_t align);

// Hex string to integer
int hextoi(const char *s, int n);

// printf declaration
void printf(const char *fmt, ...);

#endif // HELPER_H
