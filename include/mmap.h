#ifndef MMAP_H
#define MMAP_H

#define MAX_VMA_REGIONS 32

#define MMAP_PROT_READ 1
#define MMAP_PROT_WRITE 2
#define MMAP_PROT_EXEC 4
#define MMAP_ANONYMOUS 0x20
#define MMAP_POPULATE 0x8000

struct thread;

struct vm_area {
    unsigned long start;
    unsigned long end;
    int prot;
    int flags;
};

long process_mmap(void *addr, unsigned long length, int prot, int flags);
int process_handle_page_fault(unsigned long addr, unsigned long cause);
int process_install_user_image(struct thread *t, const void *image,
                               unsigned long size);

#endif /* MMAP_H */
