#include "helper.h"
#include "startup_alloc.h"
#include "task.h"
#include "timer.h"
#include "trap.h"
#include "uart.h"

extern void handle_exception_entry(void);
extern void ret_from_exception(void);

#define USER_STACK_SIZE 0x1000UL
#define TIMER_INTERVAL_TICKS 20000000ULL
#define SCAUSE_INTERRUPT_BIT (1UL << 63)
#define SCAUSE_SUPERVISOR_TIMER 5UL
#define SCAUSE_SUPERVISOR_EXTERNAL 9UL
#define SCAUSE_USER_ECALL 8UL
#define UART_IRQ_ID 10U

static uint64_t boot_hart_id;
static uint64_t boot_time_base;
static uint64_t timer_interval_ticks = TIMER_INTERVAL_TICKS;

static inline uint64_t rdtime(void) {
    uint64_t t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

static inline void enable_sstatus_sie(void) { asm volatile("csrsi sstatus, 2"); }

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
    return 0x0c000000UL + ((uint64_t)irq << 2);
}

static inline uint64_t plic_s_enable_addr(uint64_t hart) {
    return 0x0c000000UL + 0x2080UL + (hart * 0x100UL);
}

static inline uint64_t plic_s_threshold_addr(uint64_t hart) {
    return 0x0c000000UL + 0x201000UL + (hart * 0x2000UL);
}

static inline uint64_t plic_s_claim_addr(uint64_t hart) {
    return 0x0c000000UL + 0x201004UL + (hart * 0x2000UL);
}

static void plic_init(void) {
    plic_write(plic_priority_addr(UART_IRQ_ID), 1);
    plic_write(plic_s_enable_addr(boot_hart_id),
               plic_read(plic_s_enable_addr(boot_hart_id)) | (1U << UART_IRQ_ID));
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
    return (now - boot_time_base) / (timer_interval_ticks / 2);
}

static void handle_interrupt(unsigned long cause) {
    unsigned long irq = cause & ~SCAUSE_INTERRUPT_BIT;
    if (irq == SCAUSE_SUPERVISOR_TIMER) {
        timer_handle_irq();
        timer_program_next();
        return;
    }

    if (irq == SCAUSE_SUPERVISOR_EXTERNAL) {
        uint32_t claim = plic_claim();
        if (claim == UART_IRQ_ID) {
            uart_handle_irq();
        }
        if (claim != 0) {
            plic_complete(claim);
        }
    }
}

static void handle_exception(unsigned long cause, struct pt_regs *regs) {
    if (cause == SCAUSE_USER_ECALL) {
        printf("sepc: 0x%x scause: 0x%x stval: 0x%x\n", regs->epc, regs->cause,
               regs->badvaddr);
        regs->epc += 4;
        return;
    }

    printf("Unexpected exception sepc=0x%x scause=0x%x stval=0x%x\n", regs->epc,
           regs->cause, regs->badvaddr);
}

void do_trap(struct pt_regs *regs) {
    if (regs->cause & SCAUSE_INTERRUPT_BIT) {
        handle_interrupt(regs->cause);
    } else {
        handle_exception(regs->cause, regs);
    }
    task_run_pending();
}

void trap_init(uint64_t hart_id) {
    boot_hart_id = hart_id;
    boot_time_base = rdtime();
    write_stvec((void *)handle_exception_entry);
    write_sscratch(read_sp());
    plic_init();
    task_init();
    timer_init(boot_time_base, timer_interval_ticks);
    uart_enable_rx_interrupt();
    enable_sie_stie();
    enable_sie_seie();
    enable_sstatus_sie();
    timer_program_next();
}

int trap_exec_user(const void *entry, size_t size) {
    (void)size;
    void *user_stack = startup_alloc(USER_STACK_SIZE, USER_STACK_SIZE);
    if (user_stack == 0) {
        return -1;
    }

    unsigned long current_sp = read_sp();
    struct pt_regs *regs = (struct pt_regs *)(current_sp - sizeof(struct pt_regs));
    for (size_t i = 0; i < sizeof(struct pt_regs) / sizeof(unsigned long); i++) {
        ((unsigned long *)regs)[i] = 0;
    }

    regs->epc = (unsigned long)entry;
    regs->sp = (unsigned long)user_stack + USER_STACK_SIZE;
    regs->status = (1UL << 5);
    write_sscratch(current_sp);

    asm volatile("mv sp, %0\n\tj ret_from_exception" : : "r"(regs) : "memory");
    return 0;
}
