#include "vm.h"

#include "buddy.h"
#include "helper.h"
#include "kmalloc.h"

#define PGD_SHIFT 30
#define PMD_SHIFT 21
#define PTE_SHIFT 12
#define VPN_MASK 0x1ffUL
#define BOOT_LINEAR_GIB 8
#define BOOT_PGD_PA 0x80100000UL
#define BOOT_PMD_PA 0x80101000UL
#define VPN(va, shift) (unsigned int)(((va) >> (shift)) & VPN_MASK)

static inline unsigned long pte_pa(unsigned long pte) {
    return (pte >> 10) << 12;
}

void setup_vm(void) __attribute__((section(".text.boot")));
void drop_identity_map(void) __attribute__((section(".text.boot")));

void setup_vm(void) {
    unsigned long *pgd = (unsigned long *)BOOT_PGD_PA;
    unsigned long *pmd_flat = (unsigned long *)BOOT_PMD_PA;
    for (unsigned long i = 0; i < VM_ENTRIES_PER_TABLE; i++) {
        pgd[i] = 0;
    }
    for (unsigned long i = 0; i < BOOT_LINEAR_GIB * VM_ENTRIES_PER_TABLE; i++) {
        pmd_flat[i] = 0;
    }

    for (unsigned long g = 0; g < BOOT_LINEAR_GIB; g++) {
        unsigned long base = g * VM_PGD_SIZE;
        for (unsigned long p = 0; p < VM_ENTRIES_PER_TABLE; p++) {
            unsigned long pa = base + p * VM_PMD_SIZE;
            unsigned long prot = pa < 0x40000000UL ? PROT_MMIO : PROT_KERNEL;
            ((unsigned long (*)[VM_ENTRIES_PER_TABLE])BOOT_PMD_PA)[g][p] = MAKE_PTE(pa, prot);
        }

        ((unsigned long *)BOOT_PGD_PA)[VPN(base, PGD_SHIFT)] =
            MAKE_PTE(BOOT_PMD_PA + g * VM_PAGE_SIZE, PTE_V);
        ((unsigned long *)BOOT_PGD_PA)[256 + g] =
            MAKE_PTE(BOOT_PMD_PA + g * VM_PAGE_SIZE, PTE_V);
    }

    asm volatile("li t0, 8\n"
                 "slli t0, t0, 60\n"
                 "li t1, 0x80100\n"
                 "or t0, t0, t1\n"
                 "csrw satp, t0\n"
                 "sfence.vma zero, zero\n"
                 :
                 :
                 : "memory", "t0", "t1");
}

void drop_identity_map(void) {
    for (unsigned long g = 0; g < BOOT_LINEAR_GIB; g++) {
        ((unsigned long *)BOOT_PGD_PA)[VPN(g * VM_PGD_SIZE, PGD_SHIFT)] = 0;
    }
    asm volatile("sfence.vma zero, zero" ::: "memory");
}

unsigned long *vm_kernel_pgd(void) {
    return (unsigned long *)phys_to_virt(BOOT_PGD_PA);
}

static unsigned long *alloc_page_table(void) {
    void *page = allocate(VM_PAGE_SIZE);
    if (page == (void *)0) {
        return (void *)0;
    }
    memset(page, 0, VM_PAGE_SIZE);
    return (unsigned long *)page;
}

unsigned long *vm_create_user_pgd(void) {
    unsigned long *pgd = alloc_page_table();
    unsigned long *kernel = vm_kernel_pgd();
    if (pgd == (void *)0) {
        return (void *)0;
    }
    for (int i = 256; i < VM_ENTRIES_PER_TABLE; i++) {
        pgd[i] = kernel[i];
    }
    return pgd;
}

static int pagewalk(unsigned long *pgd, unsigned long va, unsigned long pa,
                    unsigned long prot) {
    unsigned long *table = pgd;
    unsigned int shifts[2] = {PGD_SHIFT, PMD_SHIFT};

    for (int level = 0; level < 2; level++) {
        unsigned int idx = VPN(va, shifts[level]);
        if ((table[idx] & PTE_V) == 0) {
            unsigned long *next = alloc_page_table();
            if (next == (void *)0) {
                return -1;
            }
            table[idx] = MAKE_PTE(virt_to_phys((unsigned long)next), PTE_V);
        }
        table = (unsigned long *)phys_to_virt(pte_pa(table[idx]));
    }

    table[VPN(va, PTE_SHIFT)] = MAKE_PTE(pa, prot);
    return 0;
}

int vm_map_pages(unsigned long *pgd, unsigned long va, unsigned long size,
                 unsigned long pa, unsigned long prot) {
    unsigned long end = va + ((size + VM_PAGE_SIZE - 1) & ~(VM_PAGE_SIZE - 1));
    for (unsigned long cur = va; cur < end; cur += VM_PAGE_SIZE) {
        if (pagewalk(pgd, cur, pa + cur - va, prot) < 0) {
            return -1;
        }
    }
    asm volatile("sfence.vma zero, zero" ::: "memory");
    return 0;
}

unsigned long vm_translate(unsigned long *pgd, unsigned long va) {
    unsigned long *table = pgd;
    unsigned int shifts[3] = {PGD_SHIFT, PMD_SHIFT, PTE_SHIFT};

    for (int level = 0; level < 3; level++) {
        unsigned long pte = table[VPN(va, shifts[level])];
        if ((pte & PTE_V) == 0) {
            return 0;
        }
        if ((pte & (PTE_R | PTE_W | PTE_X)) != 0) {
            unsigned long page_mask = (1UL << shifts[level]) - 1;
            return pte_pa(pte) + (va & page_mask);
        }
        table = (unsigned long *)phys_to_virt(pte_pa(pte));
    }
    return 0;
}

void vm_switch(unsigned long *pgd) {
    if (pgd == (void *)0) {
        pgd = vm_kernel_pgd();
    }
    asm volatile("csrw satp, %0\n"
                 "sfence.vma zero, zero\n"
                 :
                 : "r"(MAKE_SATP(virt_to_phys((unsigned long)pgd)))
                 : "memory");
}
