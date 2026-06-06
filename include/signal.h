#ifndef SIGNAL_H
#define SIGNAL_H

struct trap_frame;

long process_signal(int signum, void (*handler)(void));
void process_sigreturn(struct trap_frame *regs);
long process_kill(int pid, int signum);
void check_pending_signals(struct trap_frame *regs);
void process_clear_signal_state(void);

#endif /* SIGNAL_H */
