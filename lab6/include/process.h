#ifndef PROCESS_H
#define PROCESS_H

struct trap_frame;

void process_exit(int status);
long process_waitpid(long pid);
int process_stop(long pid);
long process_usleep(unsigned int usec);
long process_fork(struct trap_frame *regs);
int process_exec_image(const void *image, unsigned long size);
int process_spawn_user(const void *image, unsigned long size);

#endif /* PROCESS_H */
