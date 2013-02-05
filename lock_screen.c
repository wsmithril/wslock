#include <xcb/xcb.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xcb.h>
#include <stdint.h>
#include <unistd.h>

#include "lock_screen.h"
#include "timer.h"

#define MIN(x, y) ((x) > (y)? (y): (x))

extern wtimer_t * pass_wrong_timer;

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

static void draw_stripes(cairo_t * cc,
        const uint16_t width, const uint16_t height,
        const uint32_t fg,    const uint32_t bg,
        const uint16_t space, const char * text) {

    cairo_set_source_uint32(cc, fg);
    cairo_set_font_size(cc, TEXT_SIZE);
    cairo_select_font_face(cc, "Sans",
            CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

    // calculate text and stripe size
    uint16_t x = 0, y = 0, w = 0, h = 0;
    cairo_text_extents_t te;
    cairo_text_extents(cc, text, &te);

    w = te.width + 3 * te.height;
    h = te.height * 3;
    x = (width  - w) / 2;
    y = (height - h) / 2;

    // dwar the strip
    int i = 0, nstripe = ((w + h) / space) / 2 + 1;
    for (i = 0; i < nstripe; i++) {
        uint16_t x1 = space * (i * 2 + 1.5);
        uint16_t y1 = x1 > w? x1 - w: 0;
        uint16_t x2 = space * (i * 2 + 0.5);
        uint16_t y2 = x2 > w? x2 - w: 0;
        uint16_t y3 = space * (i * 2 + 0.5);
        uint16_t x3 = y3 > h? y3 - h: 0;
        uint16_t y4 = space * (i * 2 + 1.5);
        uint16_t x4 = y4 > h? y4 - h: 0;

        cairo_move_to(cc, x + MIN(x1, w), y + y1);
        if (y2 == 0 && y1 != 0) cairo_line_to(cc, x + w, y);
        cairo_line_to(cc, x + MIN(x2, w), y + y2);
        cairo_line_to(cc, x + x3, y + MIN(y3, h));
        if (x3 == 0 && x4 != 0) cairo_line_to(cc, x, y + h);
        cairo_line_to(cc, x + x4, y + MIN(y4, h));
        cairo_close_path(cc);
    }
    cairo_fill(cc);

    // draw the text
    cairo_set_source_uint32(cc, bg);
    w = te.width  + 2 * space;
    h = te.height + 2 * space;
    x = (width  - w) / 2;
    y = (height - h) / 2;
    cairo_rectangle(cc, x, y, w, h);
    cairo_fill(cc);
    cairo_set_source_uint32(cc, fg);
    cairo_move_to(cc, x + space - te.x_bearing, y + space - te.y_bearing);
    cairo_show_text(cc, text);
}

void draw_input_box(cairo_t * cc,
        const uint16_t width, const uint16_t height,
        const uint32_t fg,    const uint32_t bg,
        const uint32_t pad,   const int len) {
    // U+25CF BLACK CIRCLE, UTF-8 encoding
    static const char dot[] = {0xE2, 0x97, 0x8F, 0x00};
    cairo_set_font_size(cc, TEXT_SIZE * 0.75);
    cairo_select_font_face(cc, "Sans",
            CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);

    cairo_text_extents_t te;
    cairo_text_extents(cc, dot, &te);

    // draw the outer box
    uint16_t x = 0, y = 0, w = 0, h = 0;
    w = te.width * (MIN(len, PASS_SHOW_LEN)) +
        pad      * (MIN(len, PASS_SHOW_LEN) - 1) +
        te.height;
    h = te.height * 2;
    x = (width -  w) / 2;
    y = (height - h) / 2;

    cairo_set_source_uint32(cc, fg);
    cairo_rectangle(cc, x, y, w, h);
    cairo_set_line_width(cc, TEXT_SIZE / 10);
    cairo_stroke(cc);

    // draw text
    int i = 0;
    w = te.width * (MIN(len, PASS_SHOW_LEN)) +
        pad      * (MIN(len, PASS_SHOW_LEN) - 1);
    h = te.height;
    x = (width -  w) / 2;
    y = (height - h) / 2;
    for (i = 0; i < len; i++) {
        cairo_move_to(cc,
            x + i * (te.width + pad) - te.x_bearing, y - te.y_bearing);
        cairo_show_text(cc, dot);
    }
}

void lock_screen_input(xcb_connection_t * c, xcb_screen_t * s,
        xcb_window_t w, const int len) {
    // cached visual_type
    if (!visual_type) visual_type = get_root_visualitype(s);

    cairo_surface_t * xcb_cs = cairo_xcb_surface_create(c, w, visual_type,
            s->width_in_pixels, s->height_in_pixels);
    cairo_t * xcb_cc = cairo_create(xcb_cs);

    cairo_set_source_uint32(xcb_cc, len? COLOR_INPUT: COLOR_LOCK);

    cairo_rectangle(xcb_cc, 0, 0, s->width_in_pixels, s->height_in_pixels);
    cairo_fill(xcb_cc);

    if (len) draw_input_box(xcb_cc, s->width_in_pixels, s->height_in_pixels,
            COLOR_INPUT_FG, COLOR_INPUT, TEXT_SIZE / 5, len);
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

    // draw a red background
    cairo_set_source_uint32(xcb_cc, COLOR_WRONG);
    cairo_rectangle(xcb_cc, 0, 0, s->width_in_pixels, s->height_in_pixels);
    cairo_fill(xcb_cc);

    draw_stripes(xcb_cc, s->width_in_pixels, s->height_in_pixels,
            COLOR_WRONG_FG, COLOR_WRONG, STRIPE_WIDTH, "ACESS DENIED");

    // reset pass_wrong timer
    wtimer_rearm(pass_wrong_timer, 0, NULL);

    cairo_surface_destroy(xcb_cs);
    cairo_destroy(xcb_cc);
}
