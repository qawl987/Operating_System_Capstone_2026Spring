#ifndef INITRD_H
#define INITRD_H

void initrd_list(const void *start, const void *end);
void initrd_cat(const void *start, const void *end, const char *filename);

#endif /* INITRD_H */
