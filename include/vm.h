#ifndef VM_H
#define VM_H

#include <stddef.h>

#define PAGE_OFFSET 0xffffffc000000000UL
#define VM_PAGE_SIZE 4096UL
#define VM_PMD_SIZE (1UL << 21)
#define VM_PGD_SIZE (1UL << 30)
#define VM_ENTRIES_PER_TABLE 512

#define USER_TEXT_VA 0x0UL
#define USER_MMAP_BASE 0x2000000000UL
#define USER_MMAP_END 0x3fffe00000UL
#define USER_SIGNAL_STACK_PAGE_VA 0x3fffeff000UL
#define USER_SIGNAL_STACK_TOP 0x3ffff00000UL
#define USER_STACK_REGION_BASE 0x3ffff00000UL
#define USER_STACK_PAGE_VA 0x3ffffff000UL
#define USER_STACK_TOP 0x4000000000UL

#define PTE_V (1UL << 0)
#define PTE_R (1UL << 1)
#define PTE_W (1UL << 2)
#define PTE_X (1UL << 3)
#define PTE_U (1UL << 4)
#define PTE_G (1UL << 5)
#define PTE_A (1UL << 6)
#define PTE_D (1UL << 7)
#define PTE_COW (1UL << 8)

#define PROT_KERNEL (PTE_V | PTE_R | PTE_W | PTE_X | PTE_G | PTE_A | PTE_D)
#define PROT_MMIO (PTE_V | PTE_R | PTE_W | PTE_G | PTE_A | PTE_D)
#define PROT_USER_BASE (PTE_V | PTE_U | PTE_A | PTE_D)
#define PROT_USER_RX (PROT_USER_BASE | PTE_R | PTE_X)
#define PROT_USER_RW (PROT_USER_BASE | PTE_R | PTE_W)
#define PROT_USER_RWX (PROT_USER_BASE | PTE_R | PTE_W | PTE_X)

#define SATP_SV39 (8UL << 60)
#define MAKE_SATP(pgd_pa) (SATP_SV39 | ((unsigned long)(pgd_pa) >> 12))
#define MAKE_PTE(pa, flags) ((((unsigned long)(pa)) >> 12) << 10 | (flags))

static inline unsigned long phys_to_virt(unsigned long pa) {
    return pa + PAGE_OFFSET;
}

static inline unsigned long virt_to_phys(unsigned long va) {
    return va >= PAGE_OFFSET ? va - PAGE_OFFSET : va;
}

void setup_vm(void);
void drop_identity_map(void);
unsigned long *vm_kernel_pgd(void);
unsigned long *vm_create_user_pgd(void);
int vm_map_pages(unsigned long *pgd, unsigned long va, unsigned long size,
                 unsigned long pa, unsigned long prot);
unsigned long vm_translate(unsigned long *pgd, unsigned long va);
unsigned long *vm_get_pte(unsigned long *pgd, unsigned long va);
int vm_clone_user_cow(unsigned long *dst, unsigned long *src);
void vm_switch(unsigned long *pgd);

#endif /* VM_H */
