#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

void timer_init(uint64_t boot_time_base, uint64_t interval_ticks);
void timer_handle_irq(void);
void timer_program_next(void);
void timer_set_periodic_log_enabled(int enabled);
void timer_start_periodic_log(void);

#endif /* TIMER_H */
