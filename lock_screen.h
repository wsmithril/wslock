#ifndef _LOCK_SCREEN_H_
#define _LOCK_SCREEN_H_

#include <xcb/xcb.h>
#include <stdint.h>

// default color config
#if !defined COLOR_LOCK
#   define COLOR_LOCK (uint32_t)(0x101010)
#endif

#if !defined COLOR_INPUT
#   define COLOR_INPUT (uint32_t)(0x4e7aa7)
#endif

#if !defined COLOR_WRONG
#   define COLOR_WRONG (uint32_t)(0x9c3200)
#endif

#include <stdint.h>
extern uint32_t color_lock;
extern uint32_t color_wrong;
extern uint32_t color_input;

void lock_screen_input(xcb_connection_t * c, xcb_screen_t * s,
        xcb_window_t w, const int len);
void lock_screen_error(xcb_connection_t * c, xcb_screen_t * s,
        xcb_window_t w);

#endif
