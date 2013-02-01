#include <xcb/xcb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <stdint.h>
#include <stdio.h>

#include "lock_screen.h"

uint32_t color_lock  = 0;
uint32_t color_wrong = 0;
uint32_t color_input = 0;

static xcb_visualtype_t * get_root_visualitype(xcb_screen_t * s) {
    xcb_depth_iterator_t depth_iter;
    xcb_visualtype_iterator_t visual_iter;

    for (depth_iter = xcb_screen_allowed_depths_iterator(s);
         depth_iter.rem; xcb_depth_next (&depth_iter)) {

        for (visual_iter = xcb_depth_visuals_iterator(depth_iter.data);
             visual_iter.rem; xcb_visualtype_next (&visual_iter)) {
            if (s->root_visual == visual_iter.data->visual_id)
                return visual_iter.data;
        }
    }
    return NULL;
}

static xcb_visualtype_t * visual_type = NULL;

#define cairo_set_source_uint32(c, color) do { \
    cairo_set_source_rgb(c, \
            (((color) & 0x00ff0000) >> 16) / 255.0, \
            (((color) & 0x0000ff00) >> 8)  / 255.0, \
            (((color) & 0x000000ff) >> 0)  / 255.0); \
} while (0)

void lock_screen_input(xcb_connection_t * c, xcb_screen_t * s,
        xcb_window_t w, const int len) {
    // cached visual_type
    if (!visual_type) visual_type = get_root_visualitype(s);

    cairo_surface_t * xcb_cs = cairo_xcb_surface_create(c, w, visual_type,
            s->width_in_pixels, s->height_in_pixels);
    cairo_t * xcb_cc = cairo_create(xcb_cs);

    cairo_set_source_uint32(xcb_cc, len? color_input: color_lock);

    cairo_rectangle(xcb_cc, 0, 0, s->width_in_pixels, s->height_in_pixels);
    cairo_fill(xcb_cc);

    cairo_surface_destroy(xcb_cs);
    cairo_destroy(xcb_cc);
}

void lock_screen_error(xcb_connection_t * c, xcb_screen_t * s,
        xcb_window_t w) {
    // cached visual_type
    if (!visual_type) visual_type = get_root_visualitype(s);

    cairo_surface_t * xcb_cs = cairo_xcb_surface_create(c, w, visual_type,
            s->width_in_pixels, s->height_in_pixels);
    cairo_t * xcb_cc = cairo_create(xcb_cs);

    cairo_set_source_uint32(xcb_cc, color_wrong);
    cairo_rectangle(xcb_cc, 0, 0, s->width_in_pixels, s->height_in_pixels);
    cairo_fill(xcb_cc);

    cairo_surface_destroy(xcb_cs);
    cairo_destroy(xcb_cc);
}
