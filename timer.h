#ifndef __TIMER_H__
#define __TIMER_H__

#include <stdint.h>
#include <sys/types.h>
#include <sys/time.h>

struct wtimer_t;

typedef void (*wtimer_cb)(const struct wtimer_t * t, const struct timeval * now);

enum wtimer_type_t {
    WTIMER_TYPE_ONESHOT = 1L << 0,
    WTIMER_TYPE_ONCE    = 1L << 1,
    WTIMER_TYPE_REPEAT  = 1L << 2,
};

struct wtimer_t {
    uint32_t id;
    uint64_t timeout;
    enum wtimer_type_t type;
    enum { wt_suspend = 0, wt_running = 1 } status;
    wtimer_cb cb;
    struct timeval started;
    struct wtimer_t * next;
};
typedef struct wtimer_t wtimer_t;

typedef struct {
    wtimer_t * head;
    enum { tl_pause = 0, tl_running = 1 } status;
} wtimer_list_t;

wtimer_list_t * wtimer_list_new(void);
wtimer_t * wtimer_new(const uint64_t us, wtimer_cb cb,
        const enum wtimer_type_t type);
void wtimer_add(wtimer_list_t * tl, wtimer_t * t);
int64_t wtimer_list_next_timeout(
        wtimer_list_t * tl, const struct timeval * now);
int wtimer_list_timeout(wtimer_list_t * tl, const struct timeval * now);
void wtimer_rearm(wtimer_t * t, const uint64_t to, wtimer_cb cb);
void wtimer_list_stop(wtimer_list_t * tl);
void wtimer_list_start(wtimer_list_t * tl);

#endif
