#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include "timer.h"

static uint32_t global_id = 0;

inline
static int64_t timeval_diff(
        const struct timeval * t1, const struct timeval * t2) {
    return (t1->tv_sec - t2->tv_sec) * 1000000 + (t1->tv_usec - t2->tv_usec);
}

wtimer_list_t * wtimer_list_new(void) {
    wtimer_list_t * tl = calloc(1, sizeof(wtimer_list_t));
    return tl;
}

wtimer_t * wtimer_new(uint64_t us, wtimer_cb cb, uint32_t op) {
    wtimer_t * nt = calloc(1, sizeof(wtimer_t));
    nt->id      = global_id++;
    nt->timeout = us;
    nt->cb      = cb;
    nt->op      = op;
    return nt;
}

void wtimer_add(wtimer_list_t * tl, wtimer_t * t) {
    t->next  = tl->head;
    tl->head = t;
    // start the timer if the timer list is running
    if (tl->status) gettimeofday(&t->started, NULL);
}

int64_t wtimer_list_next_timeout(
        wtimer_list_t * tl, const struct timeval * now) {

    int64_t to = 0x7fffffffffffffffULL;
    int64_t tmp_diff = 0;
    int count = 0;
    wtimer_t * t = NULL;

    for (t = tl->head; t; t = t->next) {
        if (!t->status) continue;
        count++;
        tmp_diff = t->timeout - timeval_diff(now, &t->started);
        to = tmp_diff < to? tmp_diff: to;
    }

    if (tl->status && count) { // we have running timer
        return to > 0? to: 0;
    } else { // no running timer
        return -1;
    }
}

int wtimer_list_timeout(wtimer_list_t * tl, const struct timeval * now) {
    if (!tl->status) return -1;

    int count = 0;
    wtimer_t * t = NULL, * t_prev = NULL;

    for (t = tl->head; t; t_prev = t, t = t->next) {
        if (t->status && t->timeout < timeval_diff(now, &t->started)) {
            count++;
            (*t->cb)(t, now); // call wtimer_cb

            // remove timer from the list or reset its timeout??
            if (t->op & WTIMER_ONCE) {
                if (!t_prev) tl->head = t->next;
                else t_prev->next = t->next;
                t->status = wt_suspend;
            } else if (t->op & WTIMER_ONESHOT) {
                t->status = wt_suspend;
            } else {
                // not remove from list
                memcpy(&t->started, now, sizeof(struct timeval));
            }
        }
    }

    return count;
}

void wtimer_rearm(wtimer_t * t, const uint64_t to, wtimer_cb cb) {
    if (to) t->timeout = to;
    if (cb) t->cb = cb;
    t->status = wt_running;
    gettimeofday(&t->started, NULL);
}

void wtimer_list_stop(wtimer_list_t * tl) {
    tl->status = tl_pause;
}

void wtimer_list_start(wtimer_list_t * tl) {
    if (!tl->head || tl->status) return;
    struct timeval now;
    gettimeofday(&now, NULL);
    wtimer_t * t = NULL;
    tl->status = tl_running;
    for (t = tl->head; t; t = t->next) {
        memcpy(&t->started, &now, sizeof(struct timeval));
        t->status = wt_running;
    }
}

#ifdef __TEST_WTIMER__
#include <stdio.h>

void to_cb(const wtimer_t * t, const struct timeval * now) {
    printf("timeout on %d at %d.%ds\n", t->id, now->tv_sec, now->tv_usec);
}

int main(void) {
    wtimer_list_t * tl = wtimer_list_new();
    wtimer_t * t1 = wtimer_new(4000000, to_cb, WTIMER_ONESHOT);
    wtimer_add(tl, t1);
    wtimer_t * t2 = wtimer_new(3000000, to_cb, WTIMER_ONESHOT);
    wtimer_add(tl, t2);

    wtimer_list_start(tl);

    while (1) {
        int64_t to;
        struct timeval now;
        gettimeofday(&now, NULL);
        wtimer_list_timeout(tl, &now);
        wtimer_rearm(t2, 0, NULL);

        gettimeofday(&now, NULL);
        to = wtimer_list_next_timeout(tl, &now);
        printf("next timeout in %8.6fs\n", to * 1.0 / 1000000);
        usleep(to);
    }
}

#endif
