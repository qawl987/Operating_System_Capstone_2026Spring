#ifndef DTBPARSER_H
#define DTBPARSER_H

#include <stdint.h>

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
};

int fdt_path_offset(const void *fdt, const char *target_path);
const void *fdt_getprop(const void *fdt, int nodeoffset, const char *name,
                        int *lenp);

/* Get the first child node offset, or -1 if none */
int fdt_first_subnode(const void *fdt, int nodeoffset);

/* Get the next sibling node offset, or -1 if none */
int fdt_next_subnode(const void *fdt, int nodeoffset);

/* Get total size of DTB */
static inline uint32_t fdt_totalsize(const void *fdt) {
    const struct fdt_header *header = (const struct fdt_header *)fdt;
    return ((header->totalsize >> 24) & 0xFF) |
           ((header->totalsize >> 8) & 0xFF00) |
           ((header->totalsize << 8) & 0xFF0000) |
           ((header->totalsize << 24) & 0xFF000000);
}

#endif /* DTBPARSER_H */
