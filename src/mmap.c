#include "mmap.h"

#include "buddy.h"
#include "config.h"
#include "helper.h"
#include "kmalloc.h"
#include "thread.h"
#include "vm.h"

static unsigned long mmap_prot_to_pte(int prot) {
    unsigned long pte = PROT_USER_BASE;

    if (prot & MMAP_PROT_READ) {
        pte |= PTE_R;
    }
    if (prot & MMAP_PROT_WRITE) {
        pte |= PTE_W;
        if (prot & MMAP_PROT_READ) {
            pte |= PTE_R;
        }
    }
    if (prot & MMAP_PROT_EXEC) {
        pte |= PTE_X;
    }
    return pte;
}

static int vma_ranges_overlap(unsigned long a_start, unsigned long a_end,
                              unsigned long b_start, unsigned long b_end) {
    return a_start < b_end && b_start < a_end;
}

static struct vm_area *find_vma(struct thread *t, unsigned long addr) {
    if (t == (void *)0) {
        return (void *)0;
    }
    for (int i = 0; i < t->vma_count; i++) {
        if (addr >= t->vmas[i].start && addr < t->vmas[i].end) {
            return &t->vmas[i];
        }
    }
    return (void *)0;
}

static int vma_conflicts(struct thread *t, unsigned long start,
                         unsigned long end) {
    for (int i = 0; i < t->vma_count; i++) {
        if (vma_ranges_overlap(start, end, t->vmas[i].start, t->vmas[i].end)) {
            return 1;
        }
    }
    return 0;
}

static int map_anonymous_page(struct thread *t, unsigned long va,
                              unsigned long pte_prot) {
    void *page = allocate(VM_PAGE_SIZE);
    if (page == (void *)0) {
        return -1;
    }
    memset(page, 0, VM_PAGE_SIZE);
    if (vm_map_pages(t->pgd, va, VM_PAGE_SIZE,
                     virt_to_phys((unsigned long)page), pte_prot) < 0) {
        free(page);
        return -1;
    }
    return 0;
}

static int map_vma_page(struct thread *t, struct vm_area *vma,
                        unsigned long page_va) {
    void *page = allocate(VM_PAGE_SIZE);
    if (page == (void *)0) {
        return -1;
    }
    memset(page, 0, VM_PAGE_SIZE);

    if (vma->backing != (void *)0) {
        unsigned long off = page_va - vma->start;
        if (off < vma->backing_size) {
            unsigned long n = vma->backing_size - off;
            if (n > VM_PAGE_SIZE) {
                n = VM_PAGE_SIZE;
            }
            memcpy(page, (const char *)vma->backing + off, n);
        }
    }

    if (vm_map_pages(t->pgd, page_va, VM_PAGE_SIZE,
                     virt_to_phys((unsigned long)page),
                     mmap_prot_to_pte(vma->prot)) < 0) {
        free(page);
        return -1;
    }
    if (vma->prot & MMAP_PROT_EXEC) {
        asm volatile(".word 0x0000100f" ::: "memory");
    }
    return 0;
}

long process_mmap(void *addr, unsigned long length, int prot, int flags) {
    struct thread *cur = get_current();
    if (cur == (void *)0 || cur->pgd == (void *)0 || length == 0 ||
        (flags & MMAP_ANONYMOUS) == 0 || cur->vma_count >= MAX_VMA_REGIONS) {
        return -1;
    }

    unsigned long size = (length + VM_PAGE_SIZE - 1) & ~(VM_PAGE_SIZE - 1);
    // va = 0, !base <= va <= end, conflict
    unsigned long va = (unsigned long)addr;
    if (va == 0 || (va & (VM_PAGE_SIZE - 1)) != 0 ||
        va < USER_MMAP_BASE || va + size > USER_MMAP_END ||
        vma_conflicts(cur, va, va + size)) {
        va = cur->mmap_next;
        while (va + size <= USER_MMAP_END && vma_conflicts(cur, va, va + size)) {
            va += size;
        }
    }
    if (va + size > USER_MMAP_END) {
        return -1;
    }

    struct vm_area *vma = &cur->vmas[cur->vma_count++];
    vma->start = va;
    vma->end = va + size;
    vma->prot = prot;
    vma->flags = flags;
    vma->backing = (void *)0;
    vma->backing_size = 0;

    if (va + size > cur->mmap_next) {
        cur->mmap_next = va + size;
    }

    if (flags & MMAP_POPULATE) {
        unsigned long pte_prot = mmap_prot_to_pte(prot);
        for (unsigned long off = 0; off < size; off += VM_PAGE_SIZE) {
            if (map_anonymous_page(cur, va + off, pte_prot) < 0) {
                return -1;
            }
        }
    }
    return (long)va;
}

int process_handle_page_fault(unsigned long addr, unsigned long cause) {
    struct thread *cur = get_current();
    struct vm_area *vma = find_vma(cur, addr);
    int need = 0;
    if (cause == 12) {
        need = MMAP_PROT_EXEC;
    } else if (cause == 13) {
        need = MMAP_PROT_READ;
    } else if (cause == 15) {
        need = MMAP_PROT_WRITE;
    }

    unsigned long page_va = addr & ~(VM_PAGE_SIZE - 1);
    // work through PGD get PTE
    unsigned long *pte = vm_get_pte(cur->pgd, page_va);
    if (cause == 15 && pte != (void *)0 && (*pte & PTE_COW)) {
        if (vma == (void *)0 || (vma->prot & MMAP_PROT_WRITE) == 0) {
            return -1;
        }
        unsigned long old_pa = ((*pte >> 10) << 12);
        int old_page = addr_to_page(old_pa);
        printf("[Permission fault]: %x\n", page_va);
        // share between process
        if (get_page_ref(old_page) > 1) {
            void *page = allocate(VM_PAGE_SIZE);
            if (page == (void *)0) {
                return -1;
            }
            memcpy(page, (void *)phys_to_virt(old_pa), VM_PAGE_SIZE);
            free_pages(old_page);
            // Update the current process's PTE to point to the new page,
            // restore writability, and clear the PTE_COW flag.
            *pte = MAKE_PTE(virt_to_phys((unsigned long)page),
                            ((*pte & 0x3ffUL) | PTE_W) & ~PTE_COW);
        } else {
            // no share, just change to writable
            *pte = (*pte | PTE_W) & ~PTE_COW;
        }
        asm volatile("sfence.vma zero, zero" ::: "memory");
        return 0;
    }

    // page exist but prot & need not match
    if (vma == (void *)0 || (vma->prot & need) == 0) {
        return -1;
    }
    // page exist but can't find pa
    if (vm_translate(cur->pgd, page_va) != 0) {
        return -1;
    }
    if (map_vma_page(cur, vma, page_va) < 0) {
        return -1;
    }
    printf("[Translation fault]: %x\n", page_va);
    return 0;
}

int process_install_user_image(struct thread *t, const void *image,
                               unsigned long size) {
    if (t == (void *)0 || image == (void *)0 || size == 0 ||
        size > USER_IMAGE_SIZE) {
        return -1;
    }
    // create user PGD, also copy kernel pgd for trap/syscall
    unsigned long *pgd = vm_create_user_pgd();
    if (pgd == (void *)0) {
        return -1;
    }
    unsigned long mapped = (size + VM_PAGE_SIZE - 1) & ~(VM_PAGE_SIZE - 1);

    if (t->user_stack != (void *)0) {
        free(t->user_stack);
    }
    t->user_stack = (void *)0;
    t->pgd = pgd;
    t->user_image_size = size;
    t->mmap_next = USER_MMAP_BASE;
    t->vma_count = 0;
    t->vmas[t->vma_count++] = (struct vm_area){
        .start = USER_TEXT_VA,
        .end = USER_TEXT_VA + mapped,
        .prot = MMAP_PROT_READ | MMAP_PROT_WRITE | MMAP_PROT_EXEC,
        .flags = MMAP_ANONYMOUS,
        .backing = image,
        .backing_size = size,
    };
    t->vmas[t->vma_count++] = (struct vm_area){
        .start = USER_STACK_REGION_BASE,
        .end = USER_STACK_TOP,
        .prot = MMAP_PROT_READ | MMAP_PROT_WRITE,
        .flags = MMAP_ANONYMOUS,
        .backing = (void *)0,
        .backing_size = 0,
    };
    return 0;
}
