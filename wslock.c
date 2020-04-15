// crypt() and usleep() is not in POSIX standard, but we need them
#define _XOPEN_SOURCE 500

#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <sys/epoll.h>
#include <xcb/xcb.h>
#include <xcb/xcb_keysyms.h>
#include <xcb/xcb_image.h>

// since xcb dosen't X11/keysym.h eqvalient, for now, we needs this X11 header
#include <X11/keysym.h>

#if defined(USE_PAM)
#   include <security/pam_appl.h>
#else
#   include <pwd.h>
#   include <shadow.h>
#endif

// if we are using pam to handle auth, we then do not needs shadow
#if !defined(NO_DPMS)
#   include <xcb/dpms.h>
#endif

#include "lock_screen.h"
#include "timer.h"

// global variables
#define curs_invisible_width 8
#define curs_invisible_height 8

static unsigned char curs_invisible_bits[] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

static char * pass_input = NULL;
static xcb_connection_t * xcb_conn = NULL;

typedef struct {
    xcb_window_t lock_window;
    xcb_screen_t * screen;
} lock_t;

static lock_t * locks = NULL;
static int ns;
static int  pass_pos = 0;

#if !defined(NO_DPMS)
static void dpms_off(xcb_connection_t * c) {
    xcb_dpms_enable(c);
    xcb_dpms_force_level(c, XCB_DPMS_DPMS_MODE_OFF);
    xcb_flush(c);
}
#endif

static void die(const char * fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(EXIT_FAILURE);
}

#define MAX_PASSLEN (1024)

#if !defined(USE_PAM)

// plain text and shadow version
#   if defined(NO_SHADOW)
// this should be run as euid == root, plain version
static int get_userpasswd(char * up) {
    struct passwd * pw = getpwuid(getuid());
    if (!pw) {
        perror("getpwuid()");
        return 1;
    }
    endpwent();
    strncpy(up, pw_passwd, MAX_PASSLEN);
    return 0;
}

#   else // end of NO_SHADOW version

// this should be run as euid == root, shadow version
static int get_userpasswd(char * up) {
    struct spwd * pw = getspnam(getenv("USER"));
    if (!pw) {
        perror("getspnam()");
        return 1;
    }
    endspent();
    strcpy(up, pw->sp_pwdp);
    return 0;
}

#   endif // end of shadow version

// check passwd, shadow version
static int check_pass(const char * p1, const char * p2) {
    return strcmp(crypt(p1, p2), p2);
}

#else
static pam_handle_t * pamh = NULL;
static char * username = NULL;

static int pam_conv_func(int nmsg, const struct pam_message ** msg,
        struct pam_response ** resp, void * data) {
    int i = 0;
    *resp = calloc(nmsg, sizeof(struct pam_response));

    for (i = 0; i < nmsg; i++) {
        if (msg[i]->msg_style == PAM_PROMPT_ECHO_OFF ||
            msg[i]->msg_style == PAM_PROMPT_ECHO_ON) {
            (*resp)[i].resp = calloc(MAX_PASSLEN, sizeof(char));
            strcpy((*resp)[i].resp, pass_input);
        }
    }

    return PAM_SUCCESS;
}

static struct pam_conv pam_conv = {pam_conv_func, NULL};

static int check_pass() {
    return pam_authenticate(pamh, 0) == PAM_SUCCESS? 0: 1;
}

#endif /* NO_PAM */

static void lock(xcb_connection_t * c);
static void read_passwd(xcb_connection_t * c, const char * passwd);

// this function is stolen from i3lock, with some modification
static void clear_memory(char * p, const size_t s) {
    // using volatile pointer and fill the coresponding memory with random
    // data to prevent these call being optmized out
    int i = 0;
    volatile char * vp = p;
#if __LP64__
    for (i = 0; i < s; i++) vp[i] = i + (uint64_t)free;
#else
    for (i = 0; i < s; i++) vp[i] = i + (uint32_t)free;
#endif
}

int main(const int argc, const char * argv[]) {
    int ret = 0;

#if !defined(USE_PAM)
    char * user_pass = calloc(MAX_PASSLEN, sizeof(char));

    // passoword in plain text, should prevent it from being swapped to disk
    if ((ret = mlock(user_pass, sizeof(char) * MAX_PASSLEN))) {
        perror("mlock()");
        die("Cannot set mlock, please check RLIMIT_MEMLOCK\n");
    }

    if ((ret = get_userpasswd(user_pass)))
        die("Fail to get user password, "
            "maybe not set suid with \"chmod u+s\"?\n");

    // now we can drop root privileges
    if (setuid(getuid()) || setgid(getgid())) {
        die("Cannot drop root privileges"
            "I'll just die here before doing anything.\n");
    }
#else // init PAM
    username = getenv("USER");
    if ((ret = pam_start("wslock-password", username, &pam_conv, &pamh))
                != PAM_SUCCESS) {
        perror("pam_start()");
        die("unable to start PAM auth");
    }
#endif

    // init xcb connections
    xcb_conn = xcb_connect(NULL, NULL);
    if(!xcb_conn || xcb_connection_has_error(xcb_conn))
        die("unable to open xcb connection, just die here\n");

    ns = xcb_setup_roots_length(xcb_get_setup(xcb_conn));
    locks = calloc(ns, sizeof(lock_t));

    // lock everything
    lock(xcb_conn);

#if !defined(NO_DPMS)
    dpms_off(xcb_conn);
#endif

    // make sure we have everything synced.
    xcb_flush(xcb_conn);

    // read password, blocked till we should unlock
#if defined(USE_PAM)
    read_passwd(xcb_conn, NULL);
#else
    read_passwd(xcb_conn, user_pass);
#endif

    // free everything
    xcb_disconnect(xcb_conn);
#if !defined(USE_PAM)
    clear_memory(user_pass, MAX_PASSLEN);
    free(user_pass);
#else
    pam_end(pamh, 0);
#endif
    free(locks);

    return 0;
}

static void set_window_ontop(xcb_connection_t * c, xcb_window_t w) {
    uint32_t mask = 0;
    uint32_t values[1];

    // put to top
    mask = XCB_CONFIG_WINDOW_STACK_MODE;
    values[0] = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(c, w, mask, values);
}

static xcb_window_t new_fullscreen_window(xcb_connection_t * c,
        xcb_screen_t * s, const uint32_t color) {
    uint32_t mask = 0;
    uint32_t values[3];

    xcb_window_t win = xcb_generate_id(c);

    mask |= XCB_CW_BACK_PIXEL;
    values[0] = color;

    mask |= XCB_CW_OVERRIDE_REDIRECT;
    values[1] = 1;

    mask |= XCB_CW_EVENT_MASK;
    values[2] = XCB_EVENT_MASK_EXPOSURE |
                XCB_EVENT_MASK_KEY_PRESS |
                XCB_EVENT_MASK_KEY_RELEASE |
                XCB_EVENT_MASK_VISIBILITY_CHANGE |
                XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    xcb_create_window(
            c, XCB_COPY_FROM_PARENT, win, s->root,
            0, 0, s->width_in_pixels, s->height_in_pixels, 0,
            XCB_WINDOW_CLASS_INPUT_OUTPUT,
            s->root_visual,
            mask, values);

    xcb_map_window(c, win);

    set_window_ontop(c, win);
    return win;
}

static xcb_cursor_t create_cursor(xcb_connection_t *conn, xcb_screen_t *screen) {
    xcb_pixmap_t bitmap;
    xcb_pixmap_t mask;
    xcb_cursor_t cursor;

    unsigned char *curs_bits;
    unsigned char *mask_bits;
    int curs_w, curs_h;

    curs_bits = curs_invisible_bits;
    mask_bits = curs_invisible_bits;
    curs_w = curs_invisible_width;
    curs_h = curs_invisible_height;

    bitmap = xcb_create_pixmap_from_bitmap_data(conn,
                                                screen->root,
                                                curs_bits,
                                                curs_w,
                                                curs_h,
                                                1,
                                                screen->white_pixel,
                                                screen->black_pixel,
                                                NULL);

    mask = xcb_create_pixmap_from_bitmap_data(conn,
                                              screen->root,
                                              mask_bits,
                                              curs_w,
                                              curs_h,
                                              1,
                                              screen->white_pixel,
                                              screen->black_pixel,
                                              NULL);

    cursor = xcb_generate_id(conn);

    xcb_create_cursor(conn,
                      cursor,
                      bitmap,
                      mask,
                      65535, 65535, 65535,
                      0, 0, 0,
                      0, 0);

    xcb_free_pixmap(conn, bitmap);
    xcb_free_pixmap(conn, mask);

    return cursor;
}

// TODO: still have media keys grabbed...
static void grab_everything_excpt_mediakey(
            xcb_connection_t * c, xcb_screen_t * s){
    xcb_grab_pointer_cookie_t  pc;
    xcb_grab_pointer_reply_t * pr;

    xcb_grab_keyboard_cookie_t  kc;
    xcb_grab_keyboard_reply_t * kr;

    xcb_cursor_t cursor = create_cursor(c, s);

    int retry = 10000;

    while (retry--) {
        pc = xcb_grab_pointer(c, false, s->root, XCB_NONE,
                XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                XCB_NONE, cursor, XCB_CURRENT_TIME);
        if ((pr = xcb_grab_pointer_reply(c, pc, NULL)) &&
             pr->status == XCB_GRAB_STATUS_SUCCESS) {
            free(pr);
            break;
        }
        usleep(100);
    }

    while (retry--) {
        kc = xcb_grab_keyboard(c, true, s->root, XCB_CURRENT_TIME,
                XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC);
        if ((kr = xcb_grab_keyboard_reply(c, kc, NULL)) &&
             kr->status == XCB_GRAB_STATUS_SUCCESS) {
            free(kr);
            break;
        }
    }
}

static void lock(xcb_connection_t * c) {
    // lock each screen, one by one
    const xcb_setup_t * xcb_setup = xcb_get_setup(c);
    xcb_screen_iterator_t iter  = xcb_setup_roots_iterator(xcb_setup);
    int i = 0;

    // iterate through screens
    for (i = 0; i < ns; i++) {
        xcb_screen_t * s = iter.data;
        xcb_change_window_attributes(c, s->root, XCB_CW_EVENT_MASK,
                (uint32_t[]) { XCB_EVENT_MASK_STRUCTURE_NOTIFY });

        locks[i].lock_window = new_fullscreen_window(c, s, COLOR_LOCK);
        locks[i].screen      = s;
        grab_everything_excpt_mediakey(c, s);
        xcb_screen_next(&iter);
    }
}

void idle_cb(wtimer_t * t, const struct timeval * now) {
    dpms_off(xcb_conn);
}

// state for check password state
enum pass_check_state {
    pass_key_ignored = -1,
    pass_not_check = 0,
    pass_auth_fail = 1,
    pass_auth_succ = 2,
};

static int deal_with_key_press(
        xcb_key_press_event_t * event, xcb_key_symbols_t * kss,
        char * pass_input, int * pos, const char * pass_sys) {

    // this is nasty...
    xcb_keysym_t ks;
    if (((event->state & XCB_MOD_MASK_LOCK) &&
         (event->state & XCB_MOD_MASK_SHIFT)) ||
        (!(event->state & XCB_MOD_MASK_LOCK) &&
         !(event->state & XCB_MOD_MASK_SHIFT))) {
        ks = xcb_key_press_lookup_keysym(kss, event, 0);
    } else {
        ks = xcb_key_press_lookup_keysym(kss, event, 1);
    }

    if (xcb_is_keypad_key(ks) ||
        xcb_is_private_keypad_key(ks) ||
        xcb_is_cursor_key(ks) ||
        xcb_is_pf_key(ks) ||
        xcb_is_function_key(ks) ||
        xcb_is_misc_function_key(ks) ||
        xcb_is_modifier_key(ks)) return pass_key_ignored;

    int ret = pass_not_check;

    switch (ks) {
        case XK_Escape:
            pass_input[0] = 0;
            *pos = 0;
            break;
        case XK_Return:
        case XK_KP_Enter:
            pass_input[*pos] = 0;
#if defined(USE_PAM)
            ret = check_pass()? pass_auth_fail: pass_auth_succ;
#else
            ret = check_pass(pass_input, pass_sys)? pass_auth_fail
                                                  : pass_auth_succ;
#endif
            *pos = 0;
            break;
        case XK_BackSpace:
        case XK_Delete:
            if (*pos) pass_input[--(*pos)] = 0;
            break;
        default:
            if (*pos >= MAX_PASSLEN - 1) break;
            pass_input[(*pos)++] = ks;
            pass_input[*pos] = 0;
            break;
    }

    return ret;
}

wtimer_t * pass_wrong_timer = NULL;

static void pass_wrong_cb(wtimer_t * t, const struct timeval * now) {
    int i = 0;
    for (i = 0; i < ns; i++)
        lock_screen_input(xcb_conn, locks[i].screen, locks[i].lock_window, pass_pos);
    xcb_flush(xcb_conn);
}

#define Sec (1000 * 1000)
static void read_passwd(xcb_connection_t * c, const char * pass) {
    // init mainloop timers
    wtimer_list_t * tl = wtimer_list_new(0);
    wtimer_t * idle_timer = wtimer_new(5 * Sec, idle_cb,
            WTIMER_TYPE_REPEAT, WTIMER_OP_DEFAULT);
    wtimer_add(tl, idle_timer);
    pass_wrong_timer = wtimer_new(3 * Sec, pass_wrong_cb,
            WTIMER_TYPE_ONESHOT, WTIMER_OP_INITSUSPEND);
    wtimer_add(tl, pass_wrong_timer);

    // init keysym
    xcb_key_symbols_t * ksyms = xcb_key_symbols_alloc(c);

    // init epoll on xcb connection fd
    struct epoll_event ev;
    int epoll_fd = epoll_create(1); // size not used in kernel, 1 is fine
    int xcb_fd   = xcb_get_file_descriptor(c);

    if (epoll_fd < 0) {
        perror("epoll_create()");
        die("epoll fail, fd limit?\n");
    }

    ev.events  = EPOLLIN;
    ev.data.fd = xcb_fd;

    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, xcb_fd, &ev) < 0) {
        perror("epoll_ctl()");
        die("epoll failed\n");
    }

    // prepare memory to store user input
    pass_input = calloc(MAX_PASSLEN, sizeof(char));
    // user input in plain text, should prevent it from being swapped to disk
    if (mlock(pass_input, sizeof(char) * MAX_PASSLEN)) {
        perror("mlock()");
        die("Cannot set mlock, please check RLIMIT_MEMLOCK\n");
    }

    // the main loop
    struct timeval * now = calloc(1, sizeof(struct timeval));
    int64_t to  = -1;
    int     nev = 0, ntimer = 0;

    // start timer
    wtimer_list_start(tl);

    int exit_now = 0;
    while (!exit_now) {
        gettimeofday(now, NULL);

        to = wtimer_list_next_timeout(tl, now); // how long shall we wait

        if (to > 0) to /= 1000;
        errno = 0;
        nev = epoll_wait(epoll_fd, &ev, 1, to); // then we will wait
        switch (errno) {
            case EBADF:
            case EINVAL:
                // fd to xcb connection became unusable, maybe X crashed
                // we should exit now.
                perror("Cannot perform epoll on xcb connection fd, "
                       "Maybe X just crashed. epoll_wait()");
                exit_now = 1;
            case EINTR:
                // just epoll again
                continue;
            default: break;
        }

        // check and trigger timeouts
        ntimer = wtimer_list_timeout(tl, now);
        if (ntimer)
            ;

        if (nev) { // we got xcb events
            xcb_generic_event_t * event;
            while ((event = xcb_poll_for_event(c))) {
                if (!event->response_type) goto next_event;
                int type = (event->response_type & 0x7f);
                int i = 0, ret = 0;

#define foreach_screen for (i = 0; i < ns; i++)
                switch (type) {
                    case XCB_CIRCULATE_NOTIFY:
                        // this shouldn't be happening...
                        // unless some window sets itself on-top of the stack
                        foreach_screen
                            set_window_ontop(c, locks[i].lock_window);
                        break;

                    case XCB_KEY_PRESS:
                        ret = deal_with_key_press(
                                (xcb_key_press_event_t *)event, ksyms,
                                pass_input, &pass_pos, pass);

                        switch (ret) {
                            case pass_auth_succ:
                                exit_now = 1;
                                break;

                            case pass_auth_fail:
                                foreach_screen
                                    lock_screen_error(c, locks[i].screen,
                                        locks[i].lock_window);
                                break;

                            case pass_not_check:
                                foreach_screen
                                    lock_screen_input(c, locks[i].screen,
                                        locks[i].lock_window, pass_pos);
                                break;
                        }
                        break;

                    default: break;
                }
#undef foreach_screen
next_event:
                xcb_flush(c);
                free(event);
                wtimer_rearm(idle_timer, 0, NULL);
            }
        }
    }

    free(tl);
    free(idle_timer);
    // make sure this area of memory is wipped out
    clear_memory(pass_input, MAX_PASSLEN);
    free(pass_input);
    xcb_key_symbols_free(ksyms);
}

