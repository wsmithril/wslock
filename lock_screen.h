#ifndef _LOCK_SCREEN_H_
#define _LOCK_SCREEN_H_

#include <xcb/xcb.h>
#include <stdint.h>

// default color config
#if !defined color_lock
#   define color_lock (uint32_t)(0x101010)
#endif

#if !defined color_input
#   define color_input (uint32_t)(0x4e7aa7)
#endif

#if !defined color_wrong
#   define color_wrong (uint32_t)(0x9c3200)
#endif

void lock_screen_input(xcb_connection_t * c, xcb_screen_t * s,
        xcb_window_t w, const int len);
void lock_screen_error(xcb_connection_t * c, xcb_screen_t * s,
        xcb_window_t w);

#if !defined WARN_W
#   define WARN_W 400
#endif

#if !defined WARN_H
#   define WARN_H 200
#endif

#if !defined WARN_S
#   define WARN_S 12
#endif

#endif
