#include "task.h"
#include "uart.h"

#define TASK_LIST_MAX 64

static int adv2_priority_set[4];

struct task_event {
    int priority;
    task_callback_t callback;
    void *arg;
    struct task_event *next;
};

static struct task_event task_pool[TASK_LIST_MAX];
static struct task_event *task_free_list;
static struct task_event *task_head;
static int task_current_priority = -2147483647;

static void adv2_p1_callback(void *arg) {
    (void)arg;
    uart_puts("P1 start\n");
    uart_puts("P1 end\n");
}

static void adv2_p3_callback(void *arg) {
    (void)arg;
    uart_puts("P3 start\n");
    add_task(adv2_p1_callback, (void *)0, adv2_priority_set[0]);
    add_timer((void *)0, (void *)0, 0);
    uart_puts("P3 end\n");
}

static void adv2_p2_callback(void *arg) {
    (void)arg;
    uart_puts("P2 start\n");
    add_task(adv2_p3_callback, (void *)0, adv2_priority_set[2]);
    add_timer((void *)0, (void *)0, 0);
    uart_puts("P2 end\n");
}

static void adv2_p4_callback(void *arg) {
    (void)arg;
    uart_puts("P4 start\n");
    add_task(adv2_p2_callback, (void *)0, adv2_priority_set[1]);
    add_timer((void *)0, (void *)0, 0);
    uart_puts("P4 end\n");
}

static void adv2_test_func(void *arg) {
    (void)arg;
    int from_small_to_big = 0;
    if (from_small_to_big) {
        adv2_priority_set[0] = 10;
        adv2_priority_set[1] = 20;
        adv2_priority_set[2] = 30;
        adv2_priority_set[3] = 40;
    } else {
        adv2_priority_set[0] = 40;
        adv2_priority_set[1] = 30;
        adv2_priority_set[2] = 20;
        adv2_priority_set[3] = 10;
    }
    add_task(adv2_p4_callback, (void *)0, adv2_priority_set[3]);
}

static struct task_event *task_alloc_node(void) {
    struct task_event *n = task_free_list;
    if (n != (void *)0) {
        task_free_list = n->next;
        n->next = (void *)0;
    }
    return n;
}

static void task_free_node(struct task_event *n) {
    n->next = task_free_list;
    task_free_list = n;
}

void task_init(void) {
    task_free_list = &task_pool[0];
    for (int i = 0; i < TASK_LIST_MAX - 1; i++) {
        task_pool[i].next = &task_pool[i + 1];
    }
    task_pool[TASK_LIST_MAX - 1].next = (void *)0;
    task_head = (void *)0;
}

int add_task(task_callback_t callback, void *arg, int priority) {
    if (callback == (void *)0) {
        return -1;
    }

    struct task_event *n = task_alloc_node();
    if (n == (void *)0) {
        return -1;
    }
    n->priority = priority;
    n->callback = callback;
    n->arg = arg;

    if (task_head == (void *)0 || priority > task_head->priority) {
        n->next = task_head;
        task_head = n;
        return 0;
    }

    struct task_event *cur = task_head;
    while (cur->next != (void *)0 && cur->next->priority >= priority) {
        cur = cur->next;
    }
    n->next = cur->next;
    cur->next = n;
    return 0;
}

void task_run_pending(void) {
    while (task_head != (void *)0) {
        struct task_event *n = task_head;
        if (task_current_priority > n->priority) {
            return;
        }
        task_head = n->next;
        task_free_node(n);

        int prev_priority = task_current_priority;
        task_current_priority = n->priority;
        asm volatile("csrsi sstatus, 2");
        if (n->callback) {
            n->callback(n->arg);
        }
        asm volatile("csrci sstatus, 2");
        task_current_priority = prev_priority;
    }
}

void task_queue_adv2_test(void) { add_timer(adv2_test_func, (void *)0, 0); }
