/**
 * Buddy System Page Frame Allocator
 * Lab 3 - Memory Allocator
 */
#ifndef _BUDDY_H
#define _BUDDY_H

#include "list.h"

/* Configuration */
#define PAGE_SIZE 4096
#define MAX_ORDER 10 /* Maximum block size = 2^10 * 4KB = 4MB */

/* Frame status values */
#define FRAME_FREE (-1)      /* Free but part of larger block */
#define FRAME_ALLOCATED (-2) /* Allocated, not available */

/**
 * struct frame - Represents a page frame
 * @order:      If >= 0, this is the head of a free block of 2^order pages
 *              If FRAME_FREE, this frame is part of a larger block
 *              If FRAME_ALLOCATED, this frame is allocated
 * @refcount:   Reference count for the frame
 * @chunk_size: If > 0, this page is used as a chunk pool with this chunk size
 *              If 0, this page is allocated as a whole page
 * @list:       Linked list node for free_area
 */
struct frame {
    int order;
    int refcount;
    int chunk_size;
    struct list_head list;
};

/* Initialize the buddy system with given memory region */
void buddy_init(unsigned long base_addr, unsigned long size);

/* Allocate 2^order contiguous pages, returns page index or -1 on failure */
int alloc_pages(unsigned int order);

/* Free pages starting at given index */
void free_pages(int page_idx);

/* Get physical address from page index */
unsigned long page_to_addr(int page_idx);

/* Get page index from physical address */
int addr_to_page(unsigned long addr);

/* Debug: dump free area status */
void buddy_dump(void);

/* Test function */
void buddy_test(void);

/* Set chunk size for a page (used by kmalloc) */
void set_page_chunk_size(int page_idx, int chunk_size);

/* Get chunk size for a page (used by kfree) */
int get_page_chunk_size(int page_idx);

/**
 * memory_reserve - Reserve a memory region
 * @start: Start physical address
 * @size:  Size in bytes
 *
 * Marks pages in [start, start+size) as reserved, removing them from free
 * lists. Must be called after buddy_init().
 */
void memory_reserve(unsigned long start, unsigned long size);

/**
 * buddy_init_with_frame_array - Initialize buddy system with external frame
 * array
 * @base_addr:   Base address of managed memory
 * @size:        Size of managed memory in bytes
 * @frame_array: Pointer to pre-allocated frame array
 * @array_pages: Number of pages used by the frame array (to mark as reserved)
 *
 * This function is used with the startup allocator to dynamically allocate
 * the frame array before initializing the buddy system.
 */
void buddy_init_with_frame_array(unsigned long base_addr, unsigned long size,
                                 struct frame *frame_array,
                                 unsigned long array_pages);

/**
 * get_frame_array_size - Calculate the size needed for the frame array
 * @num_pages: Total number of pages to manage
 *
 * Returns the size in bytes needed for the frame array.
 */
static inline unsigned long get_frame_array_size(unsigned long num_pages) {
    return num_pages * sizeof(struct frame);
}

#endif /* _BUDDY_H */
