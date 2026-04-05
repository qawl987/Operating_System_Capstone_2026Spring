/**
 * Startup Allocator (Bump Pointer Allocator)
 * Lab 3 - Memory Allocator - Advanced Exercise 3
 *
 * A minimal bump-pointer allocator used during early boot to allocate
 * the page frame array before the buddy system is initialized.
 * This breaks the chicken-and-egg problem between buddy system and
 * dynamic memory allocation.
 */
#ifndef _STARTUP_ALLOC_H
#define _STARTUP_ALLOC_H

#include <stdint.h>

/* Maximum number of reserved regions we can track */
#define MAX_RESERVED_REGIONS 16

/**
 * struct reserved_region - Describes a reserved memory region
 * @start: Start physical address (inclusive)
 * @end:   End physical address (exclusive)
 */
struct reserved_region {
    uint64_t start;
    uint64_t end;
};

/**
 * startup_add_reserved - Mark a memory region as reserved
 * @start: Start physical address
 * @size:  Size in bytes
 *
 * Must be called before startup_init() to mark regions that
 * the startup allocator should avoid (DTB, kernel, initramfs, etc.)
 */
void startup_add_reserved(uint64_t start, uint64_t size);

/**
 * startup_init - Initialize the startup allocator
 * @mem_start: Start of available physical memory
 * @mem_size:  Size of available memory in bytes
 *
 * Initializes the bump pointer to the first usable address
 * after all reserved regions.
 */
void startup_init(uint64_t mem_start, uint64_t mem_size);

/**
 * startup_alloc - Allocate memory from the startup allocator
 * @size:  Number of bytes to allocate
 * @align: Alignment requirement (must be power of 2)
 *
 * Returns pointer to allocated memory, or NULL on failure.
 * The allocated memory is 4KB-aligned by default.
 */
void *startup_alloc(uint64_t size, uint64_t align);

/**
 * startup_get_current - Get current bump pointer position
 *
 * Returns the current allocation pointer. Useful for determining
 * where the buddy system's free memory starts.
 */
uint64_t startup_get_current(void);

/**
 * startup_get_mem_start - Get memory region start
 */
uint64_t startup_get_mem_start(void);

/**
 * startup_get_mem_end - Get memory region end
 */
uint64_t startup_get_mem_end(void);

/**
 * startup_is_reserved - Check if an address range overlaps with reserved
 * regions
 * @start: Start address to check
 * @end:   End address to check (exclusive)
 *
 * Returns 1 if the range overlaps with any reserved region, 0 otherwise.
 */
int startup_is_reserved(uint64_t start, uint64_t end);

/**
 * startup_dump - Print startup allocator status for debugging
 */
void startup_dump(void);

#endif /* _STARTUP_ALLOC_H */
