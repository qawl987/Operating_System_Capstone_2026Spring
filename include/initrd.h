#ifndef INITRD_H
#define INITRD_H

#include <stddef.h>

struct initrd_iter {
    const void *cur;
    const void *end;
    const char *name;
    const void *data;
    size_t size;
    int mode;
};

void initrd_iter_begin(struct initrd_iter *it, const void *start,
                       const void *end);
int initrd_iter_next(struct initrd_iter *it);

void initrd_list(const void *start, const void *end);
void initrd_cat(const void *start, const void *end, const char *filename);
const void *initrd_find_file(const void *start, const void *end,
                             const char *filename, size_t *size);

#endif /* INITRD_H */
