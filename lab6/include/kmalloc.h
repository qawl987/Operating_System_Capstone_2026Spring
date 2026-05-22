/**
 * Dynamic Memory Allocator
 * Lab 3 - Memory Allocator
 *
 * Requests pages from the buddy system and partitions them into chunk pools.
 */
#ifndef _KMALLOC_H
#define _KMALLOC_H

#include "list.h"

/* Number of chunk pool sizes */
#define NUM_POOL_SIZES 8

/* Pool sizes: 16, 32, 64, 128, 256, 512, 1024, 2048 bytes */
#define MIN_CHUNK_SIZE 16
#define MAX_CHUNK_SIZE 2048

/**
 * struct chunk - Free chunk in a pool
 * @list: Linked list node for pool's free list
 *
 * This struct is placed at the beginning of each free chunk.
 * When the chunk is allocated, this memory is available to the user.
 */
struct chunk {
    struct list_head list;
};

/**
 * struct chunk_pool - Pool of fixed-size chunks
 * @chunk_size: Size of each chunk in this pool
 * @free_list:  List head for free chunks
 */
struct chunk_pool {
    unsigned int chunk_size;
    struct list_head free_list;
};

/**
 * kmalloc_init - Initialize the dynamic memory allocator
 */
void kmalloc_init(void);

/**
 * kmalloc - Allocate memory of given size
 * @size: Number of bytes to allocate
 *
 * Returns pointer to allocated memory, or NULL on failure.
 * For size > MAX_CHUNK_SIZE, allocates whole pages from buddy system.
 */
void *kmalloc(unsigned int size);

/**
 * kfree - Free previously allocated memory
 * @ptr: Pointer returned by kmalloc
 */
void kfree(void *ptr);

/**
 * allocate - Allocate memory (spec-required API name)
 * @size: Number of bytes to allocate
 *
 * This is an alias for kmalloc.
 */
static inline void *allocate(unsigned int size) {
    return kmalloc(size);
}

/**
 * free - Free memory (spec-required API name)
 * @ptr: Pointer returned by allocate
 *
 * This is an alias for kfree.
 */
static inline void kfree_wrapper(void *ptr) {
    kfree(ptr);
}

/* Use macro to allow 'free' as the API name */
#define free(ptr) kfree_wrapper(ptr)

/**
 * kmalloc_test - Test function for the dynamic allocator
 */
void kmalloc_test(void);

/**
 * alloc_test - Spec test case (test_alloc_1)
 */
void alloc_test(void);

#endif /* _KMALLOC_H */
