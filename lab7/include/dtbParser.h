#ifndef DTBPARSER_H
#define DTBPARSER_H

#include <stdint.h>

/* Maximum number of memory regions from DTB */
#define MAX_DTB_MEM_REGIONS 16

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

/**
 * struct mem_region - Describes a memory region
 * @start: Start physical address
 * @size:  Size in bytes
 */
struct mem_region {
    uint64_t start;
    uint64_t size;
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

/**
 * fdt_get_memory_region - Get main memory region from /memory node
 * @fdt: pointer to the device tree blob
 * @region: output parameter for the memory region
 *
 * Returns 0 on success, -1 on failure.
 */
int fdt_get_memory_region(const void *fdt, struct mem_region *region);

/**
 * fdt_get_reserved_memory - Get reserved memory regions from /reserved-memory
 * @fdt: pointer to the device tree blob
 * @regions: output array for reserved regions
 * @max_regions: maximum number of regions to return
 *
 * Returns the number of regions found.
 */
int fdt_get_reserved_memory(const void *fdt, struct mem_region *regions,
                            int max_regions);
int fdt_get_timebase_frequency(const void *fdt, uint64_t *freq);

#endif /* DTBPARSER_H */
