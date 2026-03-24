#ifndef DTBPARSER_H
#define DTBPARSER_H

int fdt_path_offset(const void *fdt, const char *target_path);
const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name,
                        int *lenp);

#endif /* DTBPARSER_H */
