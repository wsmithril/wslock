#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include "timer.h"

struct wtimer_t {
    uint32_t id;
    uint64_t timeout;
    uint32_t op;
    enum wtimer_type_t type;
    enum { wt_suspend = 0, wt_running = 1 } status;
    wtimer_cb cb;
    struct timeval started;
    struct wtimer_t * next;
};
typedef struct wtimer_t wtimer_t;

struct wtimer_list_t {
    wtimer_t * head;
    uint32_t   res;
    enum { tl_pause = 0, tl_running = 1 } status;
};
typedef struct wtimer_list_t wtimer_list_t;

static uint32_t global_id = 0;

inline
static int64_t timeval_diff(
        const struct timeval * t1, const struct timeval * t2) {
    return (t1->tv_sec - t2->tv_sec) * 1000000 + (t1->tv_usec - t2->tv_usec);
}

wtimer_list_t * wtimer_list_new(const uint32_t res) {
    wtimer_list_t * tl = calloc(1, sizeof(wtimer_list_t));
    tl->res = res;
    return tl;
}

wtimer_t * wtimer_new(const uint64_t us, wtimer_cb cb,
        const enum wtimer_type_t type, const uint32_t op) {
    wtimer_t * nt = calloc(1, sizeof(wtimer_t));
    nt->id      = global_id++;
    nt->timeout = us;
    nt->cb      = cb;
    nt->type    = type;
    nt->op      = op;
    return nt;
}

void wtimer_add(wtimer_list_t * tl, wtimer_t * t) {
    t->next  = tl->head;
    tl->head = t;
    // start the timer if the timer list is running
    if (tl->status) gettimeofday(&t->started, NULL);
    t->status = t->op & WTIMER_OP_INITSUSPEND? wt_suspend: wt_running;
}

int64_t wtimer_list_next_timeout(
        wtimer_list_t * tl, const struct timeval * now) {
    if (tl->status != tl_running) return -1;

    int64_t to = 0x7fffffffffffffffULL;
    int64_t tmp_diff = 0;
    int count = 0;
    wtimer_t * t = NULL;

    for (t = tl->head; t; t = t->next) {
        if (t->status != wt_running) continue;
        count++;
        tmp_diff = t->timeout - timeval_diff(now, &t->started);
        to = tmp_diff < to? tmp_diff: to;
    }

    to = count > 0? (to > 0? to: 0): -1;
    if (tl->res > 0) to = to > tl->res? tl->res: to;
    return to;
}

int wtimer_list_timeout(wtimer_list_t * tl, const struct timeval * now) {
    if (tl->status != tl_running) return -1;

    int count = 0;
    wtimer_t ** t = NULL;

    for (t = &tl->head; *t; ) {
        wtimer_t * et = *t;
        int del = 0;

        if (et->status && et->timeout < timeval_diff(now, &et->started)) {
            count++;
            (*et->cb)(et, now); // call wtimer_cb

            // remove timer from the list or reset its timeout??
            switch (et->type) {
                case WTIMER_TYPE_ONCE:
                    *t = et->next;
                    et->status = wt_suspend;
                    del = 1;
                    break;
                case WTIMER_TYPE_ONESHOT:
                    et->status = wt_suspend;
                    break;
                case WTIMER_TYPE_REPEAT:
                    memcpy(&et->started, now, sizeof(struct timeval));
                    break;
            }
        }

        if (!del) t = &et->next;
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
        t->status = t->op & WTIMER_OP_INITSUSPEND? wt_suspend: wt_running;
    }
}

#ifdef __TEST_WTIMER__
#include <stdio.h>

void to_cb(wtimer_t * t, const struct timeval * now) {
    printf("timeout on %d at %d.%ds\n", t->id, now->tv_sec, now->tv_usec);
}

int main(void) {
    wtimer_list_t * tl = wtimer_list_new(1 * 1000 * 1000);
    wtimer_t * t1 = wtimer_new(4000000, to_cb,
            WTIMER_TYPE_ONCE, WTIMER_OP_DEFAULT);
    wtimer_add(tl, t1);
    wtimer_t * t2 = wtimer_new(3000000, to_cb,
            WTIMER_TYPE_ONESHOT, WTIMER_OP_INITSUSPEND);
    wtimer_add(tl, t2);

    wtimer_list_start(tl);
    int i = 0;
    while (1) {
        int64_t to;
        struct timeval now;
        gettimeofday(&now, NULL);
        to = wtimer_list_next_timeout(tl, &now);
        printf("next timeout in %8.3fms\n", to * 1.0 / 1000);
        int nto = wtimer_list_timeout(tl, &now);
        usleep(to);
        if (nto) wtimer_rearm(t2, 0, NULL);
    }

    free(tl);
    free(t1);
    free(t2);
}

#endif
