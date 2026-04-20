#include "timer.h"
#include "helper.h"
#include "sbi.h"
#include "task.h"
#include "trap.h"

#define TIMER_LIST_MAX 32

struct timer_event {
    uint64_t expires_at;
    timer_callback_t callback;
    void *arg;
    struct timer_event *next;
};

static struct timer_event timer_pool[TIMER_LIST_MAX];
static struct timer_event *timer_free_list;
static struct timer_event *timer_head;
static uint64_t g_boot_time_base;
static uint64_t g_interval_ticks;

static inline uint64_t rdtime(void) {
    uint64_t t;
    asm volatile("rdtime %0" : "=r"(t));
    return t;
}

static struct timer_event *timer_alloc_node(void) {
    struct timer_event *n = timer_free_list;
    if (n != (void *)0) {
        timer_free_list = n->next;
        n->next = (void *)0;
    }
    return n;
}

static void timer_free_node(struct timer_event *n) {
    n->next = timer_free_list;
    timer_free_list = n;
}

static void periodic_tick_cb(void *arg) {
    (void)arg;
    printf("[Timer] %d seconds after boot\n", (int)trap_uptime_seconds());
    if (add_timer(periodic_tick_cb, (void *)0, 2) < 0) {
        printf("[Timer] failed to schedule periodic tick\n");
    }
}

void timer_init(uint64_t boot_time_base, uint64_t interval_ticks) {
    g_boot_time_base = boot_time_base;
    g_interval_ticks = interval_ticks;
    timer_free_list = &timer_pool[0];
    for (int i = 0; i < TIMER_LIST_MAX - 1; i++) {
        timer_pool[i].next = &timer_pool[i + 1];
    }
    timer_pool[TIMER_LIST_MAX - 1].next = (void *)0;
    timer_head = (void *)0;
    add_timer(periodic_tick_cb, (void *)0, 2);
}

int add_timer(timer_callback_t callback, void *arg, int sec) {
    if (callback == (void *)0 || sec < 0) {
        return -1;
    }

    struct timer_event *n = timer_alloc_node();
    if (n == (void *)0) {
        return -1;
    }

    uint64_t now = rdtime();
    uint64_t delay_ticks = (uint64_t)sec * g_interval_ticks / 2;
    n->expires_at = now + delay_ticks;
    n->callback = callback;
    n->arg = arg;

    if (timer_head == (void *)0 || n->expires_at < timer_head->expires_at) {
        n->next = timer_head;
        timer_head = n;
        return 0;
    }

    struct timer_event *cur = timer_head;
    while (cur->next != (void *)0 && cur->next->expires_at <= n->expires_at) {
        cur = cur->next;
    }
    n->next = cur->next;
    cur->next = n;
    return 0;
}

void timer_handle_irq(void) {
    uint64_t now = rdtime();
    while (timer_head != (void *)0 && timer_head->expires_at <= now) {
        struct timer_event *n = timer_head;
        timer_head = n->next;
        timer_callback_t cb = n->callback;
        void *cb_arg = n->arg;
        timer_free_node(n);
        if (cb) {
            if (add_task((task_callback_t)cb, cb_arg, 1) < 0) {
                cb(cb_arg);
            }
        }
        now = rdtime();
    }
}

void timer_program_next(void) {
    if (timer_head != (void *)0) {
        sbi_set_timer(timer_head->expires_at);
        return;
    }
    uint64_t now = rdtime();
    sbi_set_timer(now + g_interval_ticks);
}
