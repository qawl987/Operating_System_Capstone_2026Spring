/**
 * Buddy System Page Frame Allocator
 * Lab 3 - Memory Allocator
 *
 * Converted from C++ STL version to use generic circular doubly linked list
 */

#include "buddy.h"
#include "uart.h"

/* Hardcoded memory region for Basic Exercise */
/* QEMU virt: RAM starts at 0x80000000 */
/* Use a safe region that doesn't conflict with kernel */
#define MEM_BASE 0x90000000UL
#define MEM_SIZE 0x10000000UL /* 256 MB */
#define NUM_PAGES (MEM_SIZE / PAGE_SIZE)

/* Frame array - represents all page frames */
static struct frame mem_map[NUM_PAGES];

/* Free area lists - one list per order */
static struct list_head free_area[MAX_ORDER + 1];

/* Base address of managed memory */
static unsigned long mem_base = MEM_BASE;

/* Helper macros */
#define min(a, b) ((a) < (b) ? (a) : (b))

/**
 * Get the buddy frame index for a given frame at specified order
 * Buddy is found by XORing the index with (1 << order)
 */
static inline int get_buddy_idx(int idx, unsigned int order) {
    return idx ^ (1 << order);
}

/**
 * Print log message for adding block to free list
 */
static void log_add_to_free(int idx, int order) {
    uart_puts("[+] Add page ");
    uart_puti(idx);
    uart_puts(" to order ");
    uart_puti(order);
    uart_puts(". Range: [");
    uart_puti(idx);
    uart_puts(", ");
    uart_puti(idx + (1 << order) - 1);
    uart_puts("]\n");
}

/**
 * Print log message for removing block from free list
 */
static void log_remove_from_free(int idx, int order) {
    uart_puts("[-] Remove page ");
    uart_puti(idx);
    uart_puts(" from order ");
    uart_puti(order);
    uart_puts(". Range: [");
    uart_puti(idx);
    uart_puts(", ");
    uart_puti(idx + (1 << order) - 1);
    uart_puts("]\n");
}

/**
 * Print log message for buddy found during merge
 */
static void log_buddy_found(int buddy_idx, int page_idx, int order) {
    uart_puts("[*] Buddy found! buddy idx: ");
    uart_puti(buddy_idx);
    uart_puts(" for page ");
    uart_puti(page_idx);
    uart_puts(" with order ");
    uart_puti(order);
    uart_puts("\n");
}

/**
 * Print log message for allocation
 */
static void log_alloc(unsigned long addr, int order, int idx) {
    uart_puts("[Page] Allocate 0x");
    uart_putx(addr);
    uart_puts(" at order ");
    uart_puti(order);
    uart_puts(", page ");
    uart_puti(idx);
    uart_puts("\n");
}

/**
 * Print log message for free
 */
static void log_free(unsigned long addr, int order, int idx) {
    uart_puts("[Page] Free 0x");
    uart_putx(addr);
    uart_puts(" and add back to order ");
    uart_puti(order);
    uart_puts(", page ");
    uart_puti(idx);
    uart_puts("\n");
}

/**
 * Initialize the buddy system
 * Sets up the frame array and free lists
 */
void buddy_init(unsigned long base_addr, unsigned long size) {
    int i;
    int num_pages = size / PAGE_SIZE;

    mem_base = base_addr;

    /* Initialize all free area lists */
    for (i = 0; i <= MAX_ORDER; i++) {
        INIT_LIST_HEAD(&free_area[i]);
    }

    /* Initialize all frames */
    for (i = 0; i < num_pages; i++) {
        mem_map[i].order = FRAME_FREE;
        mem_map[i].refcount = 0;
        mem_map[i].chunk_size = 0;
        INIT_LIST_HEAD(&mem_map[i].list);
    }

    /* Add all max-order blocks to the free list */
    for (i = 0; i < num_pages; i += (1 << MAX_ORDER)) {
        mem_map[i].order = MAX_ORDER;
        list_add_tail(&mem_map[i].list, &free_area[MAX_ORDER]);
        log_add_to_free(i, MAX_ORDER);
    }

    uart_puts("Buddy system initialized: ");
    uart_puti(num_pages);
    uart_puts(" pages, base=0x");
    uart_putx(base_addr);
    uart_puts("\n");
}

/**
 * Allocate 2^order contiguous pages
 * Returns page index or -1 on failure
 */
int alloc_pages(unsigned int order) {
    int current_order;
    int idx;
    struct frame *page;

    /* Find the smallest order with available blocks */
    current_order = -1;
    for (int i = order; i <= MAX_ORDER; i++) {
        if (!list_empty(&free_area[i])) {
            current_order = i;
            break;
        }
    }

    /* No available block found */
    if (current_order == -1) {
        uart_puts("[!] alloc_pages: out of memory for order ");
        uart_puti(order);
        uart_puts("\n");
        return -1;
    }

    /* Get a block from the free list */
    page = list_first_entry(&free_area[current_order], struct frame, list);
    idx = page - mem_map;
    list_del_init(&page->list);
    log_remove_from_free(idx, current_order);

    /* Split the block until we reach the requested order */
    while (current_order > (int)order) {
        current_order--;

        /* Split: put the upper half (buddy) back to free list */
        int buddy_idx = idx + (1 << current_order);
        struct frame *buddy = &mem_map[buddy_idx];

        buddy->order = current_order;
        list_add_tail(&buddy->list, &free_area[current_order]);
        log_add_to_free(buddy_idx, current_order);

        /* Update the lower half */
        page->order = current_order;
    }

    /* Mark as allocated */
    page->order = FRAME_ALLOCATED;
    page->refcount = 1;

    /* Store the original order in a way we can retrieve it */
    /* We'll use a simple approach: store order in refcount's high bits
     * temporarily */
    /* Actually, let's just keep track of the order during allocation */
    page->order = order; /* We'll set it back for free to know the size */
    page->refcount = 1;

    log_alloc(page_to_addr(idx), order, idx);

    return idx;
}

/**
 * Free pages starting at given index
 * Attempts to coalesce with buddy blocks
 */
void free_pages(int page_idx) {
    struct frame *page = &mem_map[page_idx];
    int current_order;
    int cur_idx = page_idx;

    /* Decrease reference count */
    page->refcount--;
    if (page->refcount > 0) {
        return;
    }

    current_order = page->order;

    /* Try to merge with buddy iteratively */
    while (current_order < MAX_ORDER) {
        int buddy_idx = get_buddy_idx(cur_idx, current_order);

        /* Check if buddy exists within bounds */
        if (buddy_idx < 0 || buddy_idx >= NUM_PAGES) {
            break;
        }

        struct frame *buddy = &mem_map[buddy_idx];

        /* Check if buddy is free and has the same order */
        if (buddy->refcount != 0 || buddy->order != current_order) {
            break;
        }

        log_buddy_found(buddy_idx, cur_idx, current_order);

        /* Remove buddy from its free list */
        list_del_init(&buddy->list);
        log_remove_from_free(buddy_idx, current_order);

        /* Mark buddy as part of larger block */
        buddy->order = FRAME_FREE;

        /* Merge: use the lower address block as the new head */
        cur_idx = min(cur_idx, buddy_idx);
        current_order++;

        /* Update the merged block */
        mem_map[cur_idx].order = current_order;
    }

    /* Add the (possibly merged) block to the free list */
    page = &mem_map[cur_idx];
    page->order = current_order;
    page->refcount = 0;
    list_add_tail(&page->list, &free_area[current_order]);

    log_free(page_to_addr(page_idx), current_order, cur_idx);
    log_add_to_free(cur_idx, current_order);
}

/**
 * Get physical address from page index
 */
unsigned long page_to_addr(int page_idx) {
    return mem_base + (unsigned long)page_idx * PAGE_SIZE;
}

/**
 * Get page index from physical address
 */
int addr_to_page(unsigned long addr) { return (addr - mem_base) / PAGE_SIZE; }

/**
 * Set chunk size for a page (used by kmalloc)
 */
void set_page_chunk_size(int page_idx, int chunk_size) {
    if (page_idx >= 0 && page_idx < NUM_PAGES) {
        mem_map[page_idx].chunk_size = chunk_size;
    }
}

/**
 * Get chunk size for a page (used by kfree)
 */
int get_page_chunk_size(int page_idx) {
    if (page_idx >= 0 && page_idx < NUM_PAGES) {
        return mem_map[page_idx].chunk_size;
    }
    return 0;
}

/**
 * Debug: dump free area status
 */
void buddy_dump(void) {
    int count;
    struct list_head *pos;

    uart_puts("\n=== Buddy System Status ===\n");
    for (int i = MAX_ORDER; i >= 0; i--) {
        count = 0;
        list_for_each(pos, &free_area[i]) { count++; }
        uart_puts("free_area[");
        uart_puti(i);
        uart_puts("] = ");
        uart_puti(count);
        uart_puts(" blocks (");
        uart_puti(count * (1 << i));
        uart_puts(" pages)\n");
    }
    uart_puts("===========================\n");
}

/* ========== Test Code ========== */

void buddy_test(void) {
    int p1, p2, p3;

    uart_puts("\n===== Buddy System Test =====\n");

    /* Initialize with hardcoded region */
    buddy_init(MEM_BASE, MEM_SIZE);
    buddy_dump();

    uart_puts("\n--- Allocating p1 (order 1) ---\n");
    p1 = alloc_pages(1);
    buddy_dump();

    uart_puts("\n--- Allocating p2 (order 1) ---\n");
    p2 = alloc_pages(1);
    buddy_dump();

    uart_puts("\n--- Allocating p3 (order 1) ---\n");
    p3 = alloc_pages(1);
    buddy_dump();

    uart_puts("\n--- Freeing p1 ---\n");
    free_pages(p1);

    uart_puts("\n--- Freeing p2 ---\n");
    free_pages(p2);

    uart_puts("\n--- Freeing p3 ---\n");
    free_pages(p3);

    uart_puts("\n--- Final Status ---\n");
    buddy_dump();

    uart_puts("===== Test Complete =====\n");
}
