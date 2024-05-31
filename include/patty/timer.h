#ifndef _PATTY_TIMER_H
#define _PATTY_TIMER_H

#include <stdint.h>
#include <sys/time.h>

enum patty_timer_flags {
    PATTY_TIMER_RUNNING = (1 << 0)
};

typedef struct _patty_timer {
    time_t ms;
    struct timespec t;
    uint32_t flags;
} patty_timer;

void patty_timer_init(patty_timer *timer, time_t ms);

int patty_timer_running(patty_timer *timer);

int patty_timer_expired(patty_timer *timer);

void patty_timer_clear(patty_timer *timer);

void patty_timer_start(patty_timer *timer);

void patty_timer_stop(patty_timer *timer);

void patty_timer_sub(struct timespec *a,
                     struct timespec *b,
                     struct timespec *c);

void patty_timer_tick(patty_timer *timer,
                      struct timespec *elapsed);

#endif /* _PATTY_TIMER_H */
