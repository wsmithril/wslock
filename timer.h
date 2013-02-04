#ifndef __TIMER_H__
#define __TIMER_H__

#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>

typedef struct wtimer_t wtimer_t;
typedef struct wtimer_list_t wtimer_list_t;

typedef void (*wtimer_cb)(wtimer_t * t, const struct timeval * now);

enum wtimer_type_t {
    WTIMER_TYPE_ONESHOT = 1L << 0,
    WTIMER_TYPE_ONCE    = 1L << 1,
    WTIMER_TYPE_REPEAT  = 1L << 2,
};

enum wtimer_option_t {
    WTIMER_OP_DEFAULT     = 1L << 0,
    WTIMER_OP_INITSUSPEND = 1L << 1,
};

wtimer_list_t * wtimer_list_new(const uint32_t res);
wtimer_t * wtimer_new(const uint64_t us, wtimer_cb cb,
        const enum wtimer_type_t type, const uint32_t op);
void wtimer_add(wtimer_list_t * tl, wtimer_t * t);
int64_t wtimer_list_next_timeout(
        wtimer_list_t * tl, const struct timeval * now);
int wtimer_list_timeout(wtimer_list_t * tl, const struct timeval * now);
void wtimer_rearm(wtimer_t * t, const uint64_t to, wtimer_cb cb);
void wtimer_list_stop(wtimer_list_t * tl);
void wtimer_list_start(wtimer_list_t * tl);

#endif
