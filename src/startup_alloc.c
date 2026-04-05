/**
 * Startup Allocator (Bump Pointer Allocator)
 * Lab 3 - Memory Allocator - Advanced Exercise 3
 */

#include "startup_alloc.h"
#include "buddy.h"
#include "config.h"
#include "dtbParser.h"
#include "helper.h"
#include "kmalloc.h"
#include "logger.h"
#include "uart.h"

/* Reserved regions array */
static struct reserved_region reserved_regions[MAX_RESERVED_REGIONS];
static int num_reserved = 0;

/* Memory region managed by startup allocator */
static uint64_t mem_region_start = 0;
static uint64_t mem_region_end = 0;

/* Current bump pointer position */
static uint64_t bump_ptr = 0;

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
        log_info("[Startup] Warning: too many reserved regions\n");
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
            log_info("[Startup] Merged reserved region: 0x%x - 0x%x\n",
                     reserved_regions[i].start, reserved_regions[i].end);
            return;
        }
    }

    reserved_regions[num_reserved].start = start;
    reserved_regions[num_reserved].end = end;
    num_reserved++;

    /* Log reserve for spec requirement */
    log_spec("[Reserve] Reserve address [0x%x, 0x%x)\n", start, end);

    log_info("[Startup] Reserved region: 0x%x - 0x%x (%d KB)\n", start, end,
             (unsigned int)(size / 1024));
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
    bump_ptr = align_up_val(first_usable, 4096);

    log_info("[Startup] Initialized: memory 0x%x - 0x%x\n", mem_region_start,
             mem_region_end);
    log_info("[Startup] First usable address: 0x%x\n", bump_ptr);
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
    uint64_t aligned_ptr = align_up_val(bump_ptr, align);

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
                aligned_ptr = align_up_val(reserved_regions[i].end, align);
                progress = 1;
            }
        }
    }

    uint64_t alloc_end = aligned_ptr + size;

    /* Check bounds */
    if (alloc_end > mem_region_end) {
        log_info("[Startup] Error: out of memory\n");
        return (void *)0;
    }

    /* Advance bump pointer */
    bump_ptr = alloc_end;

    log_info("[Startup] Allocated 0x%x - 0x%x (%d KB)\n", aligned_ptr,
             alloc_end, (unsigned int)(size / 1024));

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
    log_info("\n=== Startup Allocator Status ===\n");
    log_info("Memory region: 0x%x - 0x%x\n", mem_region_start, mem_region_end);
    log_info("Current pointer: 0x%x\n", bump_ptr);
    log_info("Reserved regions (%d):\n", num_reserved);
    for (int i = 0; i < num_reserved; i++) {
        log_info("  [%d] 0x%x - 0x%x\n", i, reserved_regions[i].start,
                 reserved_regions[i].end);
    }
    log_info("================================\n");
}

/**
 * Initialize the entire memory subsystem
 */
void startup_memory_init(void *dtb_base, uint64_t initrd_start,
                         uint64_t initrd_end) {
    struct mem_region mem;
    struct mem_region reserved[MAX_DTB_MEM_REGIONS];
    int reserved_count;

    /* 1. Get memory region from DTB */
    if (fdt_get_memory_region(dtb_base, &mem) < 0) {
        log_info("[!] Failed to get memory region from DTB, using default\n");
        mem.start = 0x80000000UL;
        mem.size = 0x80000000UL; /* 2GB default */
    }

    log_info("Memory region: 0x%x - 0x%x (%d MB)\n", mem.start,
             mem.start + mem.size, (int)(mem.size / (1024 * 1024)));

    /* 2. Mark reserved regions BEFORE initializing startup allocator */

    /* Reserve DTB blob */
    uint64_t dtb_start = (uint64_t)dtb_base;
    uint64_t dtb_size = fdt_totalsize(dtb_base);
    log_info("Reserving DTB: 0x%x - 0x%x\n", dtb_start, dtb_start + dtb_size);
    startup_add_reserved(dtb_start, dtb_size);

    /* Reserve Kernel image */
    uint64_t kernel_start = (uint64_t)_kernel_start;
    uint64_t kernel_end_addr = (uint64_t)_kernel_end;
    uint64_t kernel_size = kernel_end_addr - kernel_start;
    log_info("Reserving Kernel: 0x%x - 0x%x\n", kernel_start, kernel_end_addr);
    startup_add_reserved(kernel_start, kernel_size);

    /* Reserve bootloader relocation area */
    uint64_t reloc_size = (uint64_t)_load_end - (uint64_t)_load_start;
    log_info("Reserving RELOC area: 0x%x - 0x%x\n", (uint64_t)RELOC_ADDR,
             (uint64_t)RELOC_ADDR + reloc_size);
    startup_add_reserved(RELOC_ADDR, reloc_size);

    /* Reserve Initramfs (if present) */
    if (initrd_start && initrd_end && initrd_end > initrd_start) {
        log_info("Reserving Initramfs: 0x%x - 0x%x\n", initrd_start,
                 initrd_end);
        startup_add_reserved(initrd_start, initrd_end - initrd_start);
    }

    /* Parse and reserve /reserved-memory regions from DTB */
    reserved_count =
        fdt_get_reserved_memory(dtb_base, reserved, MAX_DTB_MEM_REGIONS);
    if (reserved_count > 0) {
        log_info("Parsing /reserved-memory...\n");
        for (int i = 0; i < reserved_count; i++) {
            startup_add_reserved(reserved[i].start, reserved[i].size);
        }
    } else {
        log_info("No /reserved-memory node found\n");
    }

    /* 3. Initialize startup allocator */
    startup_init(mem.start, mem.size);
    startup_dump();

    /* 4. Calculate frame array size and allocate it */
    uint64_t total_pages = mem.size / PAGE_SIZE;
    uint64_t frame_array_size = get_frame_array_size(total_pages);
    /* Round up to page size */
    frame_array_size = (frame_array_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    uint64_t array_pages = frame_array_size / PAGE_SIZE;

    log_info("Frame array: %d pages (%d KB) for %d total pages\n",
             (int)array_pages, (int)(frame_array_size / 1024),
             (int)total_pages);

    struct frame *frame_array =
        (struct frame *)startup_alloc(frame_array_size, PAGE_SIZE);
    if (!frame_array) {
        log_info("[!] Failed to allocate frame array!\n");
        return;
    }

    /* Also mark the frame array itself as reserved */
    startup_add_reserved((uint64_t)frame_array, frame_array_size);

    /* 5. Get the current bump pointer position - everything up to here is
     * reserved */
    uint64_t reserved_end = startup_get_current();

    /* 6. Initialize buddy system with the dynamically allocated frame array */
    buddy_init_with_frame_array(mem.start, mem.size, frame_array, array_pages,
                                reserved_end);

    /* 7. Initialize dynamic allocator */
    kmalloc_init();

    log_info("Memory initialization complete.\n");
    buddy_dump();
}
