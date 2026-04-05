/**
 * Startup Allocator (Bump Pointer Allocator)
 * Lab 3 - Memory Allocator - Advanced Exercise 3
 */

#include "startup_alloc.h"
#include "uart.h"

/* Reserved regions array */
static struct reserved_region reserved_regions[MAX_RESERVED_REGIONS];
static int num_reserved = 0;

/* Memory region managed by startup allocator */
static uint64_t mem_region_start = 0;
static uint64_t mem_region_end = 0;

/* Current bump pointer position */
static uint64_t bump_ptr = 0;

/* Helper: align up to given alignment */
static inline uint64_t align_up(uint64_t val, uint64_t align) {
    return (val + align - 1) & ~(align - 1);
}

/**
 * Check if two ranges overlap
 */
static int ranges_overlap(uint64_t s1, uint64_t e1, uint64_t s2, uint64_t e2) {
    return !(e1 <= s2 || e2 <= s1);
}

/**
 * Add a reserved region
 */
void startup_add_reserved(uint64_t start, uint64_t size) {
    if (num_reserved >= MAX_RESERVED_REGIONS) {
        uart_puts("[Startup] Warning: too many reserved regions\n");
        return;
    }

    if (size == 0) {
        return;
    }

    uint64_t end = start + size;

    /* Check for duplicates or overlaps and merge if possible */
    for (int i = 0; i < num_reserved; i++) {
        if (ranges_overlap(start, end, reserved_regions[i].start,
                           reserved_regions[i].end)) {
            /* Merge overlapping regions */
            if (start < reserved_regions[i].start) {
                reserved_regions[i].start = start;
            }
            if (end > reserved_regions[i].end) {
                reserved_regions[i].end = end;
            }
            uart_puts("[Startup] Merged reserved region: 0x");
            uart_putx(reserved_regions[i].start);
            uart_puts(" - 0x");
            uart_putx(reserved_regions[i].end);
            uart_puts("\n");
            return;
        }
    }

    reserved_regions[num_reserved].start = start;
    reserved_regions[num_reserved].end = end;
    num_reserved++;

    uart_puts("[Startup] Reserved region: 0x");
    uart_putx(start);
    uart_puts(" - 0x");
    uart_putx(end);
    uart_puts(" (");
    uart_puti((unsigned int)(size / 1024));
    uart_puts(" KB)\n");
}

/**
 * Check if an address range overlaps with any reserved region
 */
int startup_is_reserved(uint64_t start, uint64_t end) {
    for (int i = 0; i < num_reserved; i++) {
        if (ranges_overlap(start, end, reserved_regions[i].start,
                           reserved_regions[i].end)) {
            return 1;
        }
    }
    return 0;
}

/**
 * Find the first usable address after all reserved regions
 * that falls within the memory region
 */
static uint64_t find_first_usable(uint64_t start, uint64_t end) {
    uint64_t current = start;

    /* Keep advancing past reserved regions */
    int progress = 1;
    while (progress && current < end) {
        progress = 0;
        for (int i = 0; i < num_reserved; i++) {
            /* If current falls within a reserved region, skip past it */
            if (current >= reserved_regions[i].start &&
                current < reserved_regions[i].end) {
                current = reserved_regions[i].end;
                progress = 1;
            }
        }
    }

    return current;
}

/**
 * Initialize the startup allocator
 */
void startup_init(uint64_t mem_start, uint64_t mem_size) {
    mem_region_start = mem_start;
    mem_region_end = mem_start + mem_size;

    /* Find first usable address (4KB aligned) */
    uint64_t first_usable = find_first_usable(mem_start, mem_region_end);
    bump_ptr = align_up(first_usable, 4096);

    uart_puts("[Startup] Initialized: memory 0x");
    uart_putx(mem_region_start);
    uart_puts(" - 0x");
    uart_putx(mem_region_end);
    uart_puts("\n");
    uart_puts("[Startup] First usable address: 0x");
    uart_putx(bump_ptr);
    uart_puts("\n");
}

/**
 * Allocate memory using bump pointer
 */
void *startup_alloc(uint64_t size, uint64_t align) {
    if (size == 0) {
        return (void *)0;
    }

    /* Default alignment is 4KB (page size) */
    if (align == 0) {
        align = 4096;
    }

    /* Align the bump pointer */
    uint64_t aligned_ptr = align_up(bump_ptr, align);

    /* Skip over any reserved regions */
    int progress = 1;
    while (progress) {
        progress = 0;
        for (int i = 0; i < num_reserved; i++) {
            uint64_t alloc_end = aligned_ptr + size;
            /* Check if allocation would overlap with reserved region */
            if (ranges_overlap(aligned_ptr, alloc_end,
                               reserved_regions[i].start,
                               reserved_regions[i].end)) {
                /* Move past this reserved region */
                aligned_ptr = align_up(reserved_regions[i].end, align);
                progress = 1;
            }
        }
    }

    uint64_t alloc_end = aligned_ptr + size;

    /* Check bounds */
    if (alloc_end > mem_region_end) {
        uart_puts("[Startup] Error: out of memory\n");
        return (void *)0;
    }

    /* Advance bump pointer */
    bump_ptr = alloc_end;

    uart_puts("[Startup] Allocated 0x");
    uart_putx(aligned_ptr);
    uart_puts(" - 0x");
    uart_putx(alloc_end);
    uart_puts(" (");
    uart_puti((unsigned int)(size / 1024));
    uart_puts(" KB)\n");

    return (void *)aligned_ptr;
}

/**
 * Get current bump pointer position
 */
uint64_t startup_get_current(void) { return bump_ptr; }

/**
 * Get memory region start
 */
uint64_t startup_get_mem_start(void) { return mem_region_start; }

/**
 * Get memory region end
 */
uint64_t startup_get_mem_end(void) { return mem_region_end; }

/**
 * Dump startup allocator status
 */
void startup_dump(void) {
    uart_puts("\n=== Startup Allocator Status ===\n");
    uart_puts("Memory region: 0x");
    uart_putx(mem_region_start);
    uart_puts(" - 0x");
    uart_putx(mem_region_end);
    uart_puts("\n");
    uart_puts("Current pointer: 0x");
    uart_putx(bump_ptr);
    uart_puts("\n");
    uart_puts("Reserved regions (");
    uart_puti(num_reserved);
    uart_puts("):\n");
    for (int i = 0; i < num_reserved; i++) {
        uart_puts("  [");
        uart_puti(i);
        uart_puts("] 0x");
        uart_putx(reserved_regions[i].start);
        uart_puts(" - 0x");
        uart_putx(reserved_regions[i].end);
        uart_puts("\n");
    }
    uart_puts("================================\n");
}
