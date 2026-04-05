/**
 * Dynamic Memory Allocator
 * Lab 3 - Memory Allocator
 *
 * Requests pages from the buddy system and partitions them into chunk pools.
 */

#include "kmalloc.h"
#include "buddy.h"
#include "uart.h"

/* Pool sizes in bytes */
static const unsigned int pool_sizes[NUM_POOL_SIZES] = {16,  32,  64,   128,
                                                        256, 512, 1024, 2048};

/* Chunk pools - one for each size */
static struct chunk_pool pools[NUM_POOL_SIZES];

/**
 * Find the pool index for a given size
 * Returns the index of the smallest pool that can fit the size,
 * or -1 if size is larger than MAX_CHUNK_SIZE
 */
static int find_pool_index(unsigned int size) {
    for (int i = 0; i < NUM_POOL_SIZES; i++) {
        if (size <= pool_sizes[i]) {
            return i;
        }
    }
    return -1;
}

/**
 * Calculate required order for buddy allocation
 * Returns the order needed to allocate at least 'size' bytes
 */
static unsigned int size_to_order(unsigned int size) {
    unsigned int order = 0;
    unsigned int pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    while ((1U << order) < pages) {
        order++;
    }
    return order;
}

/**
 * Log message for chunk allocation
 */
static void log_chunk_alloc(void *addr, unsigned int chunk_size) {
    uart_puts("[Chunk] Allocate 0x");
    uart_putx((unsigned long)addr);
    uart_puts(" at chunk size ");
    uart_puti(chunk_size);
    uart_puts("\n");
}

/**
 * Log message for chunk free
 */
static void log_chunk_free(void *addr, unsigned int chunk_size) {
    uart_puts("[Chunk] Free 0x");
    uart_putx((unsigned long)addr);
    uart_puts(" at chunk size ");
    uart_puti(chunk_size);
    uart_puts("\n");
}

/**
 * Log message for page allocation via kmalloc
 */
static void log_page_alloc(void *addr, unsigned int size, unsigned int order) {
    uart_puts("[Kmalloc] Allocate page 0x");
    uart_putx((unsigned long)addr);
    uart_puts(" for size ");
    uart_puti(size);
    uart_puts(" (order ");
    uart_puti(order);
    uart_puts(")\n");
}

/**
 * Log message for page free via kfree
 */
static void log_page_free(void *addr) {
    uart_puts("[Kmalloc] Free page 0x");
    uart_putx((unsigned long)addr);
    uart_puts("\n");
}

/**
 * Allocate a new page and partition it into chunks for the given pool
 */
static int expand_pool(int pool_idx) {
    struct chunk_pool *pool = &pools[pool_idx];
    unsigned int chunk_size = pool->chunk_size;
    int page_idx;
    unsigned long page_addr;
    unsigned int num_chunks;
    unsigned int offset;

    /* Allocate a single page from buddy system */
    page_idx = alloc_pages(0);
    if (page_idx < 0) {
        uart_puts("[!] expand_pool: failed to allocate page\n");
        return -1;
    }

    page_addr = page_to_addr(page_idx);

    /* Mark this page as belonging to this chunk pool */
    set_page_chunk_size(page_idx, chunk_size);

    uart_puts("[Chunk] New page 0x");
    uart_putx(page_addr);
    uart_puts(" for pool size ");
    uart_puti(chunk_size);
    uart_puts("\n");

    /* Partition the page into chunks */
    num_chunks = PAGE_SIZE / chunk_size;

    for (offset = 0; offset < PAGE_SIZE; offset += chunk_size) {
        struct chunk *c = (struct chunk *)(page_addr + offset);
        INIT_LIST_HEAD(&c->list);
        list_add_tail(&c->list, &pool->free_list);
    }

    uart_puts("[Chunk] Created ");
    uart_puti(num_chunks);
    uart_puts(" chunks\n");

    return 0;
}

/**
 * Initialize the dynamic memory allocator
 */
void kmalloc_init(void) {
    uart_puts("Initializing dynamic memory allocator...\n");

    for (int i = 0; i < NUM_POOL_SIZES; i++) {
        pools[i].chunk_size = pool_sizes[i];
        INIT_LIST_HEAD(&pools[i].free_list);
    }

    uart_puts("Dynamic allocator ready. Pool sizes: ");
    for (int i = 0; i < NUM_POOL_SIZES; i++) {
        uart_puti(pool_sizes[i]);
        if (i < NUM_POOL_SIZES - 1) {
            uart_puts(", ");
        }
    }
    uart_puts("\n");
}

/**
 * Allocate memory of given size
 */
void *kmalloc(unsigned int size) {
    int pool_idx;
    struct chunk_pool *pool;
    struct chunk *c;
    void *addr;

    if (size == 0) {
        return (void *)0;
    }

    /* For large allocations, use buddy system directly */
    if (size > MAX_CHUNK_SIZE) {
        unsigned int order = size_to_order(size);

        /* Check if order is within bounds */
        if (order > MAX_ORDER) {
            uart_puts("[!] kmalloc: size too large\n");
            return (void *)0;
        }

        int page_idx = alloc_pages(order);
        if (page_idx < 0) {
            return (void *)0;
        }

        addr = (void *)page_to_addr(page_idx);
        log_page_alloc(addr, size, order);
        return addr;
    }

    /* Find appropriate pool */
    pool_idx = find_pool_index(size);
    if (pool_idx < 0) {
        uart_puts("[!] kmalloc: invalid size\n");
        return (void *)0;
    }

    pool = &pools[pool_idx];

    /* If pool is empty, expand it */
    if (list_empty(&pool->free_list)) {
        if (expand_pool(pool_idx) < 0) {
            return (void *)0;
        }
    }

    /* Get a chunk from the pool */
    c = list_first_entry(&pool->free_list, struct chunk, list);
    list_del_init(&c->list);

    addr = (void *)c;
    log_chunk_alloc(addr, pool->chunk_size);

    return addr;
}

/**
 * Free previously allocated memory
 */
void kfree(void *ptr) {
    unsigned long addr;
    unsigned long page_base;
    int page_idx;
    int chunk_size;
    int pool_idx;

    if (ptr == (void *)0) {
        return;
    }

    addr = (unsigned long)ptr;

    /* Calculate the page base address */
    page_base = addr & ~(PAGE_SIZE - 1);
    page_idx = addr_to_page(page_base);

    /* Get chunk_size from the frame info */
    chunk_size = get_page_chunk_size(page_idx);

    /* If chunk_size is 0, this was a full page allocation */
    if (chunk_size == 0) {
        log_page_free(ptr);
        free_pages(page_idx);
        return;
    }

    /* Find the pool index for this chunk size */
    pool_idx = find_pool_index(chunk_size);
    if (pool_idx < 0 || pools[pool_idx].chunk_size != (unsigned int)chunk_size) {
        uart_puts("[!] kfree: invalid chunk size ");
        uart_puti(chunk_size);
        uart_puts(" for 0x");
        uart_putx(addr);
        uart_puts("\n");
        return;
    }

    /* Add chunk back to the pool's free list */
    struct chunk *c = (struct chunk *)ptr;
    INIT_LIST_HEAD(&c->list);
    list_add_tail(&c->list, &pools[pool_idx].free_list);

    log_chunk_free(ptr, chunk_size);
}

/**
 * Test function for the dynamic allocator
 */
void kmalloc_test(void) {
    uart_puts("\n===== Dynamic Allocator Test =====\n");

    /* Test small allocations */
    uart_puts("\n--- Testing small allocations ---\n");
    char *p1 = (char *)kmalloc(16);
    char *p2 = (char *)kmalloc(32);
    char *p3 = (char *)kmalloc(64);
    char *p4 = (char *)kmalloc(128);

    uart_puts("\n--- Freeing small allocations ---\n");
    kfree(p1);
    kfree(p2);
    kfree(p3);
    kfree(p4);

    /* Test reuse */
    uart_puts("\n--- Testing reuse ---\n");
    char *p5 = (char *)kmalloc(16);
    char *p6 = (char *)kmalloc(32);
    kfree(p5);
    kfree(p6);

    /* Test large allocations (> MAX_CHUNK_SIZE) */
    uart_puts("\n--- Testing large allocations ---\n");
    char *p7 = (char *)kmalloc(4000);
    char *p8 = (char *)kmalloc(8000);
    kfree(p7);
    kfree(p8);

    /* Test multiple allocations from same pool */
    uart_puts("\n--- Testing multiple allocations ---\n");
    void *ptrs[10];
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(128);
    }
    for (int i = 0; i < 10; i++) {
        kfree(ptrs[i]);
    }

    uart_puts("\n===== Test Complete =====\n");
}

/**
 * Spec test case (test_alloc_1)
 * Uses the spec-required allocate/free API
 */
void alloc_test(void) {
    /* Define MAX_ALLOC_SIZE based on buddy system limits */
    /* MAX_ORDER = 10, so max = 2^10 * 4KB = 4MB */
    #define MAX_ALLOC_SIZE (PAGE_SIZE * (1 << MAX_ORDER))

    uart_puts("\n===== Spec Test Case (test_alloc_1) =====\n");

    uart_puts("Testing memory allocation...\n");
    char *ptr1 = (char *)allocate(4000);
    char *ptr2 = (char *)allocate(8000);
    char *ptr3 = (char *)allocate(4000);
    char *ptr4 = (char *)allocate(4000);

    free(ptr1);
    free(ptr2);
    free(ptr3);
    free(ptr4);

    /* Test kmalloc */
    uart_puts("Testing dynamic allocator...\n");
    char *kmem_ptr1 = (char *)allocate(16);
    char *kmem_ptr2 = (char *)allocate(32);
    char *kmem_ptr3 = (char *)allocate(64);
    char *kmem_ptr4 = (char *)allocate(128);

    free(kmem_ptr1);
    free(kmem_ptr2);
    free(kmem_ptr3);
    free(kmem_ptr4);

    char *kmem_ptr5 = (char *)allocate(16);
    char *kmem_ptr6 = (char *)allocate(32);

    free(kmem_ptr5);
    free(kmem_ptr6);

    /* Test allocate new page if the cache is not enough */
    uart_puts("Testing pool expansion (100 x 128 bytes)...\n");
    void *kmem_ptr[102];
    for (int i = 0; i < 100; i++) {
        kmem_ptr[i] = (char *)allocate(128);
    }
    for (int i = 0; i < 100; i++) {
        free(kmem_ptr[i]);
    }

    /* Test exceeding the maximum size */
    uart_puts("Testing allocation beyond MAX_ALLOC_SIZE...\n");
    char *kmem_ptr7 = (char *)allocate(MAX_ALLOC_SIZE + 1);
    if (kmem_ptr7 == (void *)0) {
        uart_puts("Allocation failed as expected for size > MAX_ALLOC_SIZE\n");
    } else {
        uart_puts("Unexpected allocation success for size > MAX_ALLOC_SIZE\n");
        free(kmem_ptr7);
    }

    uart_puts("\n===== Spec Test Complete =====\n");
}
