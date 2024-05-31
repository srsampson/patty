#include <patty/timer.h>

void patty_timer_init(patty_timer *timer, time_t ms) {
    timer->ms = ms;
}

int patty_timer_running(patty_timer *timer) {
    return (timer->flags & PATTY_TIMER_RUNNING)? 1: 0;
}

int patty_timer_expired(patty_timer *timer) {
    if (!(timer->flags & PATTY_TIMER_RUNNING)) {
        return 0;
    }

    return timer->t.tv_sec < 0
       || (timer->t.tv_sec == 0 && timer->t.tv_nsec <= 0)? 1: 0;
}

void patty_timer_clear(patty_timer *timer) {
    timer->t.tv_sec  = 0;
    timer->t.tv_nsec = 0;

    timer->flags &= ~PATTY_TIMER_RUNNING;
}

void patty_timer_start(patty_timer *timer) {
    timer->t.tv_sec  =  timer->ms / 1000;
    timer->t.tv_nsec = (timer->ms % 1000) * 1000000;

    timer->flags |= PATTY_TIMER_RUNNING;
}

void patty_timer_stop(patty_timer *timer) {
    timer->flags &= ~PATTY_TIMER_RUNNING;
}

void patty_timer_sub(struct timespec *a,
                     struct timespec *b,
                     struct timespec *c) {
    c->tv_nsec  = a->tv_nsec - b->tv_nsec;
    c->tv_sec   = a->tv_sec  - b->tv_sec - (c->tv_nsec / 1000000000);
    c->tv_nsec %= 1000000000;
}

void patty_timer_tick(patty_timer *timer,
                      struct timespec *elapsed) {
    struct timespec res;

    if (!(timer->flags & PATTY_TIMER_RUNNING)) {
        return;
    }

    patty_timer_sub(&timer->t, elapsed, &res);

    timer->t.tv_sec  = res.tv_sec;
    timer->t.tv_nsec = res.tv_nsec;
}
