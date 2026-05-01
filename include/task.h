#ifndef TASK_H
#define TASK_H

#include "trap.h"

void task_init(void);
void task_run_pending(void);
void task_queue_adv2_test(void);

#endif /* TASK_H */
