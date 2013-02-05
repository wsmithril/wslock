#ifndef _LOCK_SCREEN_H_
#define _LOCK_SCREEN_H_

#include <xcb/xcb.h>
#include <stdint.h>

// default color config
#if !defined COLOR_LOCK
#   define COLOR_LOCK (uint32_t)(0x101010)
#endif

#if !defined COLOR_INPUT_FG
#   define COLOR_INPUT_FG (uint32_t)(0xe0e0e0)
#endif

#if !defined COLOR_INPUT
#   define COLOR_INPUT (uint32_t)(0x4e7aa7)
#endif

#if !defined COLOR_WRONG_FG
#   define COLOR_WRONG_FG (uint32_t)(0xefcf20)
#endif

#if !defined COLOR_WRONG
#   define COLOR_WRONG (uint32_t)(0x9c3200)
#endif

void lock_screen_input(xcb_connection_t * c, xcb_screen_t * s,
        xcb_window_t w, const int len);
void lock_screen_error(xcb_connection_t * c, xcb_screen_t * s,
        xcb_window_t w);

#if !defined PASS_SHOW_LEN
#   define PASS_SHOW_LEN 16
#endif

#if !defined TEXT_SIZE
#   define TEXT_SIZE 65
#endif

#if !defined STRIPE_WIDTH
#   define STRIPE_WIDTH 17
#endif

#endif
