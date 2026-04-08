/**
 * Buddy System Page Frame Allocator
 * Lab 3 - Memory Allocator
 *
 * Converted from C++ STL version to use generic circular doubly linked list
 */

#include "buddy.h"
#include "config.h"
#include "logger.h"
#include "uart.h"

/* Maximum supported memory size (2 GB for full QEMU virt memory) */
/* 2GB = 524288 pages, each frame ~28 bytes = ~14MB frame array */
#define MAX_MEM_SIZE 0x80000000UL /* 2 GB */
#define MAX_NUM_PAGES (MAX_MEM_SIZE / PAGE_SIZE)

/*
 * Frame array - can be statically or dynamically allocated
 * When using startup allocator, mem_map_ptr points to dynamically allocated
 * array Otherwise, it points to the static array for backwards compatibility
 */
static struct frame static_mem_map[MAX_NUM_PAGES];
static struct frame *mem_map = static_mem_map; /* Default to static array */

/* Free area lists - one list per order */
static struct list_head free_area[MAX_ORDER + 1];

/* Base address and size of managed memory */
static unsigned long mem_base = 0;
static unsigned long num_pages = 0;

/* Whether using dynamic frame array */
static int using_dynamic_array = 0;

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
 * Initialize the buddy system
 * Sets up the frame array and free lists
 */
void buddy_init(unsigned long base_addr, unsigned long size) {
    unsigned long i;
    unsigned long init_num_pages = size / PAGE_SIZE;

    /* Limit to maximum supported pages */
    if (init_num_pages > MAX_NUM_PAGES) {
        init_num_pages = MAX_NUM_PAGES;
        log_info("[!] Memory limited to %d pages\n", MAX_NUM_PAGES);
    }

    mem_base = base_addr;
    num_pages = init_num_pages;
    mem_map = static_mem_map; /* Use static array */
    using_dynamic_array = 0;

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
        log_spec("[+] Add page %d to order %d. Range: [%d, %d]\n", (int)i,
                 MAX_ORDER, (int)i, (int)(i + (1 << MAX_ORDER) - 1));
    }

    log_info("Buddy system initialized: %d pages, base=0x%x\n", (int)num_pages,
             base_addr);
}

/**
 * Initialize buddy system with externally allocated frame array
 * Used by the startup allocator for dynamic frame array allocation
 */
void buddy_init_with_frame_array(unsigned long base_addr, unsigned long size,
                                 struct frame *frame_array,
                                 unsigned long array_pages,
                                 unsigned long reserved_end) {
    unsigned long i;
    unsigned long init_num_pages = size / PAGE_SIZE;

    /* Limit to maximum supported pages */
    if (init_num_pages > MAX_NUM_PAGES) {
        init_num_pages = MAX_NUM_PAGES;
        log_info("[!] Memory limited to %d pages\n", MAX_NUM_PAGES);
    }

    mem_base = base_addr;
    num_pages = init_num_pages;
    mem_map = frame_array; /* Use dynamically allocated array */
    using_dynamic_array = 1;

    log_info("[Buddy] Using dynamic frame array at 0x%x (%d pages)\n",
             (unsigned long)frame_array, (int)array_pages);

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

    /*
     * Calculate which pages are used by the frame array and
     * mark pages before reserved_end as allocated (these include
     * kernel, DTB, initramfs, frame array itself, etc.)
     */
    unsigned long reserved_pages = 0;
    if (reserved_end > base_addr) {
        reserved_pages = (reserved_end - base_addr + PAGE_SIZE - 1) / PAGE_SIZE;
    }

    log_info("[Buddy] Marking first %d pages as reserved (up to 0x%x)\n",
             (int)reserved_pages, reserved_end);

    /* Mark reserved pages as allocated */
    for (i = 0; i < reserved_pages && i < num_pages; i++) {
        mem_map[i].order = FRAME_ALLOCATED;
        mem_map[i].refcount = 1;
    }

    /* Add remaining max-order blocks to the free list */
    /* Start from the first max-order aligned page after reserved region */
    unsigned long first_free =
        ((reserved_pages + (1 << MAX_ORDER) - 1) / (1 << MAX_ORDER)) *
        (1 << MAX_ORDER);

    for (i = first_free; i < num_pages; i += (1 << MAX_ORDER)) {
        mem_map[i].order = MAX_ORDER;
        list_add_tail(&mem_map[i].list, &free_area[MAX_ORDER]);
        log_spec("[+] Add page %d to order %d. Range: [%d, %d]\n", (int)i,
                 MAX_ORDER, (int)i, (int)(i + (1 << MAX_ORDER) - 1));
    }

    log_info("Buddy system initialized with dynamic array: %d total pages, %d "
             "free, base=0x%x\n",
             (int)num_pages, (int)(num_pages - reserved_pages), base_addr);
}

/**
 * Print current free list block counts at each order level
 */
static void print_free_list_status(void) {
    log_spec("[Free] ");
    for (int i = MAX_ORDER; i >= 0; i--) {
        int count = 0;
        struct list_head *pos;
        list_for_each(pos, &free_area[i]) { count++; }
        log_spec("O%d:%d ", i, count);
    }
    log_spec("\n");
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
        log_info("[!] alloc_pages: out of memory for order %d\n", order);
        return -1;
    }

    /* Get a block from the free list */
    page = list_first_entry(&free_area[current_order], struct frame, list);
    idx = page - mem_map;
    list_del_init(&page->list);
    log_spec("[-] Remove page %d from order %d. Range: [%d, %d]\n", idx,
             current_order, idx, idx + (1 << current_order) - 1);

    /* Split the block until we reach the requested order */
    while (current_order > (int)order) {
        current_order--;

        /* Split: put the upper half (buddy) back to free list */
        int buddy_idx = idx + (1 << current_order);
        struct frame *buddy = &mem_map[buddy_idx];

        buddy->order = current_order;
        list_add_tail(&buddy->list, &free_area[current_order]);
        log_spec("[+] Add page %d to order %d. Range: [%d, %d]\n", buddy_idx,
                 current_order, buddy_idx,
                 buddy_idx + (1 << current_order) - 1);

        /* Update the lower half */
        page->order = current_order;
    }

    /* Mark as allocated */
    page->order = order; /* We'll set it back for free to know the size */
    page->refcount = 1;

    log_spec("[Page] Allocate 0x%x at order %d, page %d\n", page_to_addr(idx),
             order, idx);
    print_free_list_status();

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
        if (buddy_idx < 0 || buddy_idx >= (int)num_pages) {
            break;
        }

        struct frame *buddy = &mem_map[buddy_idx];

        /* Check if buddy is free and has the same order */
        if (buddy->refcount != 0 || buddy->order != current_order) {
            break;
        }

        log_spec("[*] Buddy found! buddy idx: %d for page %d with order %d\n",
                 buddy_idx, cur_idx, current_order);

        /* Remove buddy from its free list */
        list_del_init(&buddy->list);
        log_spec("[-] Remove page %d from order %d. Range: [%d, %d]\n",
                 buddy_idx, current_order, buddy_idx,
                 buddy_idx + (1 << current_order) - 1);

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

    log_spec("[Page] Free 0x%x and add back to order %d, page %d\n",
             page_to_addr(page_idx), current_order, cur_idx);
    log_spec("[+] Add page %d to order %d. Range: [%d, %d]\n", cur_idx,
             current_order, cur_idx, cur_idx + (1 << current_order) - 1);
    print_free_list_status();
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
    if (page_idx >= 0 && page_idx < (int)num_pages) {
        mem_map[page_idx].chunk_size = chunk_size;
    }
}

/**
 * Get chunk size for a page (used by kfree)
 */
int get_page_chunk_size(int page_idx) {
    if (page_idx >= 0 && page_idx < (int)num_pages) {
        return mem_map[page_idx].chunk_size;
    }
    return 0;
}

/**
 * Deprecated Now! Use startup allocator instead. Reserve a memory region by
 * removing pages from free lists Algorithm: iterate from MAX_ORDER down to 0,
 * split blocks that overlap with reserved region until all overlapping pages
 * are removed.
 */
void memory_reserve(unsigned long start, unsigned long size) {
    unsigned long end = start + size;

    /* Convert to page frame numbers relative to mem_base */
    if (start < mem_base) {
        if (end <= mem_base)
            return; /* Region is below managed memory */
        start = mem_base;
    }
    if (end > mem_base + (unsigned long)num_pages * PAGE_SIZE) {
        end = mem_base + (unsigned long)num_pages * PAGE_SIZE;
    }

    unsigned long start_pfn = (start - mem_base) / PAGE_SIZE;
    unsigned long end_pfn = (end - mem_base + PAGE_SIZE - 1) / PAGE_SIZE;

    if (start_pfn >= end_pfn)
        return;

    log_spec("[Reserve] Reserve address [0x%x, 0x%x)\n", start, end);

    /* Iterate from MAX_ORDER down to 0 */
    for (int order = MAX_ORDER; order >= 0; order--) {
        struct list_head *pos, *n;

        list_for_each_safe(pos, n, &free_area[order]) {
            struct frame *curr = list_entry(pos, struct frame, list);
            unsigned long block_start_pfn = curr - mem_map;
            unsigned long block_end_pfn = block_start_pfn + (1 << order);

            /* Case 1: No overlap - skip */
            if (block_end_pfn <= start_pfn || block_start_pfn >= end_pfn) {
                continue;
            }

            /* Case 2: Block is entirely within reserved region - remove it */
            if (block_start_pfn >= start_pfn && block_end_pfn <= end_pfn) {
                list_del_init(&curr->list);
                curr->order = FRAME_ALLOCATED;
                curr->refcount = 1;
                log_spec("[-] Remove page %d from order %d. Range: [%d, %d]\n",
                         (int)block_start_pfn, order, (int)block_start_pfn,
                         (int)(block_start_pfn + (1 << order) - 1));
                continue;
            }

            /* Case 3: Partial overlap - split the block */
            if (order > 0) {
                list_del_init(&curr->list);
                log_spec("[-] Remove page %d from order %d. Range: [%d, %d]\n",
                         (int)block_start_pfn, order, (int)block_start_pfn,
                         (int)(block_start_pfn + (1 << order) - 1));

                int next_order = order - 1;
                unsigned long buddy_pfn = block_start_pfn + (1 << next_order);

                /* Add the lower half */
                curr->order = next_order;
                list_add_tail(&curr->list, &free_area[next_order]);
                log_spec("[+] Add page %d to order %d. Range: [%d, %d]\n",
                         (int)block_start_pfn, next_order, (int)block_start_pfn,
                         (int)(block_start_pfn + (1 << next_order) - 1));

                /* Add the upper half (buddy) */
                struct frame *buddy = &mem_map[buddy_pfn];
                buddy->order = next_order;
                INIT_LIST_HEAD(&buddy->list);
                list_add_tail(&buddy->list, &free_area[next_order]);
                log_spec("[+] Add page %d to order %d. Range: [%d, %d]\n",
                         (int)buddy_pfn, next_order, (int)buddy_pfn,
                         (int)(buddy_pfn + (1 << next_order) - 1));
            } else {
                /* order 0 with partial overlap means this single page overlaps
                 */
                list_del_init(&curr->list);
                curr->order = FRAME_ALLOCATED;
                curr->refcount = 1;
                log_spec("[-] Remove page %d from order %d. Range: [%d, %d]\n",
                         (int)block_start_pfn, order, (int)block_start_pfn,
                         (int)(block_start_pfn + (1 << order) - 1));
            }
        }
    }
}

/**
 * Debug: dump free area status
 */
void buddy_dump(void) {
    int count;
    struct list_head *pos;

    log_info("\n=== Buddy System Status ===\n");
    for (int i = MAX_ORDER; i >= 0; i--) {
        count = 0;
        list_for_each(pos, &free_area[i]) { count++; }
        log_info("free_area[%d] = %d blocks (%d pages)\n", i, count,
                 count * (1 << i));
    }
    log_info("===========================\n");
}

/* ========== Test Code ========== */

void buddy_test(void) {
    int p1, p2, p3;

    printf("\n===== Buddy System Test =====\n");

    /* Initialize with platform-specific test region */
    buddy_init(TEST_MEM_BASE, TEST_MEM_SIZE);
    buddy_dump();

    printf("\n--- Allocating p1 (order 1) ---\n");
    p1 = alloc_pages(1);
    buddy_dump();

    printf("\n--- Allocating p2 (order 1) ---\n");
    p2 = alloc_pages(1);
    buddy_dump();

    printf("\n--- Allocating p3 (order 1) ---\n");
    p3 = alloc_pages(1);
    buddy_dump();

    printf("\n--- Freeing p1 ---\n");
    free_pages(p1);

    printf("\n--- Freeing p2 ---\n");
    free_pages(p2);

    printf("\n--- Freeing p3 ---\n");
    free_pages(p3);

    printf("\n--- Final Status ---\n");
    buddy_dump();

    printf("===== Test Complete =====\n");
}
