#include "task.h"

#define TASK_LIST_MAX 64

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
