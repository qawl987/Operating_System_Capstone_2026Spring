#ifndef SYSCALL_H
#define SYSCALL_H

#include "trap.h"

void syscall_set_initrd(unsigned long start, unsigned long end);
void syscall_handler(struct pt_regs *regs);

#endif /* SYSCALL_H */
