#include "helper.h"
#include "config.h"
#include "syscall.h"
#include "task.h"
#include "thread.h"
#include "timer.h"
#include "trap.h"
#include "uart.h"

extern void handle_exception_entry(void);
extern void ret_from_exception(void);

#define TIMER_INTERVAL_TICKS 20000000ULL
#define SCAUSE_INTERRUPT_BIT (1UL << 63)
#define SCAUSE_SUPERVISOR_TIMER 5UL
#define SCAUSE_SUPERVISOR_EXTERNAL 9UL
#define SCAUSE_USER_ECALL 8UL
#define SSTATUS_SPP (1UL << 8)

static uint64_t boot_hart_id;
static uint64_t boot_time_base;
static uint64_t timer_tick_hz = TIMER_TICK_HZ;
static uint64_t timer_interval_ticks = TIMER_INTERVAL_TICKS;
static int loader_mode;

static inline uint64_t rdtime(void) {
    uint64_t t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

static inline void enable_sstatus_sie(void) { asm volatile("csrsi sstatus, 2"); }
static inline void disable_sstatus_sie(void) { asm volatile("csrci sstatus, 2"); }

static inline void enable_sie_stie(void) {
    asm volatile("li t0, (1 << 5)\n\tcsrs sie, t0" : : : "t0");
}

static inline void enable_sie_seie(void) {
    asm volatile("li t0, (1 << 9)\n\tcsrs sie, t0" : : : "t0");
}

static inline void write_stvec(void *addr) {
    asm volatile("csrw stvec, %0" : : "r"(addr));
}

static inline void write_sscratch(unsigned long val) {
    asm volatile("csrw sscratch, %0" : : "r"(val));
}

static inline unsigned long read_sp(void) {
    unsigned long sp;
    asm volatile("mv %0, sp" : "=r"(sp));
    return sp;
}

static inline void plic_write(uint64_t addr, uint32_t val) {
    *(volatile uint32_t *)addr = val;
}

static inline uint32_t plic_read(uint64_t addr) {
    return *(volatile uint32_t *)addr;
}

static inline uint64_t plic_priority_addr(uint32_t irq) {
    return PLIC_BASE + ((uint64_t)irq << 2);
}

static inline uint64_t plic_s_enable_addr(uint64_t hart) {
    return PLIC_BASE + PLIC_S_ENABLE_BASE + (hart * 0x80UL);
}

static inline uint64_t plic_s_enable_word_addr(uint64_t hart, uint32_t irq) {
    return plic_s_enable_addr(hart) + (((uint64_t)irq / 32ULL) * 4ULL);
}

static inline uint64_t plic_s_threshold_addr(uint64_t hart) {
    return PLIC_BASE + PLIC_S_THRESHOLD_BASE + (hart * 0x2000UL);
}

static inline uint64_t plic_s_claim_addr(uint64_t hart) {
    return PLIC_BASE + PLIC_S_CLAIM_BASE + (hart * 0x2000UL);
}

static void plic_init(void) {
    uint64_t en_addr = plic_s_enable_word_addr(boot_hart_id, UART0_IRQ_ID);
    uint32_t en = plic_read(en_addr);
    plic_write(plic_priority_addr(UART0_IRQ_ID), 1);
    en |= (1U << (UART0_IRQ_ID % 32U));
    plic_write(en_addr, en);
    plic_write(plic_s_threshold_addr(boot_hart_id), 0);
}

static uint32_t plic_claim(void) { return plic_read(plic_s_claim_addr(boot_hart_id)); }

static void plic_complete(uint32_t irq) {
    plic_write(plic_s_claim_addr(boot_hart_id), irq);
}

uint64_t trap_uptime_seconds(void) {
    uint64_t now = rdtime();
    if (now < boot_time_base) {
        return 0;
    }
    return (now - boot_time_base) / timer_tick_hz;
}

void trap_enter_loader_mode(void) {
    loader_mode = 1;
    disable_sstatus_sie();
}

static void handle_interrupt(unsigned long cause, struct pt_regs *regs) {
    unsigned long irq = cause & ~SCAUSE_INTERRUPT_BIT;
    if (irq == SCAUSE_SUPERVISOR_TIMER) {
        thread_wake_sleepers(rdtime());
        timer_handle_irq();
        timer_program_next();
        if (regs != (void *)0 && (regs->status & SSTATUS_SPP) == 0 &&
            get_current() != (void *)0 && get_current()->state == THREAD_RUNNING) {
            schedule();
        }
        return;
    }

    if (irq == SCAUSE_SUPERVISOR_EXTERNAL) {
        uint32_t claim = plic_claim();
        if (claim == UART0_IRQ_ID) {
            uart_handle_irq();
        }
        if (claim != 0) {
            plic_complete(claim);
        }
    }
}

static void handle_exception(unsigned long cause, struct pt_regs *regs) {
    if (cause == SCAUSE_USER_ECALL) {
        regs->epc += 4;
        syscall_handler(regs);
        return;
    }

    printf("Unexpected exception sepc=0x%x scause=0x%x stval=0x%x\n", regs->epc,
           regs->cause, regs->badvaddr);
}

void do_trap(struct pt_regs *regs) {
    if (loader_mode) {
        return;
    }
    if (regs->cause & SCAUSE_INTERRUPT_BIT) {
        handle_interrupt(regs->cause, regs);
    } else {
        handle_exception(regs->cause, regs);
    }
    task_run_pending();
    if ((regs->status & SSTATUS_SPP) == 0) {
        check_pending_signals((struct trap_frame *)regs);
    }
}

void trap_init(uint64_t hart_id, uint64_t tick_hz) {
    loader_mode = 0;
    boot_hart_id = hart_id;
    boot_time_base = rdtime();
    if (tick_hz > 0) {
        timer_tick_hz = tick_hz;
    } else {
        timer_tick_hz = TIMER_TICK_HZ;
    }
    timer_interval_ticks = timer_tick_hz / 32ULL;
    if (timer_interval_ticks == 0) {
        timer_interval_ticks = 1;
    }
    write_stvec((void *)handle_exception_entry);
    write_sscratch(read_sp());
    plic_init();
    task_init();
    timer_init(boot_time_base, timer_tick_hz);
    uart_enable_rx_interrupt();
    enable_sie_stie();
    enable_sie_seie();
    enable_sstatus_sie();
    timer_program_next();
}

int trap_exec_user(const void *entry, size_t size) {
    int pid = process_spawn_user(entry, (unsigned long)size);
    if (pid < 0) {
        return -1;
    }
    process_waitpid(pid);
    return 0;
}
