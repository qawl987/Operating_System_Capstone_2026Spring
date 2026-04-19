#ifndef INITRD_H
#define INITRD_H

#include <stddef.h>

void initrd_list(const void *start, const void *end);
void initrd_cat(const void *start, const void *end, const char *filename);
const void *initrd_find_file(const void *start, const void *end,
                             const char *filename, size_t *size);

#endif /* INITRD_H */
