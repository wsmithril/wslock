#if !defined(USE_PAM)
#   define _XOPEN_SOURCE 500
#endif

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

#include <unistd.h>

#include "timer.h"

static xcb_connection_t * xcb_conn = NULL;

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

#else // TODO: pam part

#endif /* NO_PAM */

typedef struct {
    xcb_window_t lock_window;
} lock_t;

static void lock(xcb_connection_t * c, lock_t * lock, const int ns);
static void unlock(xcb_connection_t * c, lock_t * lock, const int ns);
static void read_passwd(xcb_connection_t * c, const char * passwd,
        const lock_t * lock, const int ns);

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
#endif

    // init xcb connections
    xcb_conn = xcb_connect(NULL, NULL);
    if(!xcb_conn || xcb_connection_has_error(xcb_conn))
        die("unable to open xcb connection, just die here\n");

    int nscreen = xcb_setup_roots_length(xcb_get_setup(xcb_conn));
    lock_t * locks = calloc(nscreen, sizeof(lock_t));

    // lock everything
    lock(xcb_conn, locks, nscreen);

#if !defined(NO_DPMS)
    dpms_off(xcb_conn);
#endif

    // make sure we have everything synced.
    xcb_flush(xcb_conn);

    // read password, blocked till we should unlock
#if defined(USE_PAM)
    read_passwd(xcb_conn, NULL, locks, nscreen);
#else
    read_passwd(xcb_conn, user_pass, locks, nscreen);
#endif

    // free everything
    xcb_disconnect(xcb_conn);
#if !defined(USE_PAM)
    clear_memory(user_pass, MAX_PASSLEN);
    free(user_pass);
#endif
    free(locks);

    return 0;
}

inline static uint32_t rgb_to_uint32(const char * hex) {
    char strgroups[3][3] = {{hex[0], hex[1], '\0'},
                            {hex[2], hex[3], '\0'},
                            {hex[4], hex[5], '\0'}};
    uint32_t rgb16[3] = {(strtol(strgroups[0], NULL, 16)),
                         (strtol(strgroups[1], NULL, 16)),
                         (strtol(strgroups[2], NULL, 16))};
    return (rgb16[0] << 16) + (rgb16[1] << 8) + rgb16[2];
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
        xcb_screen_t * s, const char * color) {
    uint32_t mask = 0;
    uint32_t values[3];

    xcb_window_t win = xcb_generate_id(c);

    mask |= XCB_CW_BACK_PIXEL;
    values[0] = rgb_to_uint32(color);

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

static void grab_everything_excpt_mediakey(
        xcb_connection_t * c, xcb_screen_t * s){
    xcb_grab_pointer_cookie_t  pc;
    xcb_grab_pointer_reply_t * pr;

    xcb_grab_keyboard_cookie_t  kc;
    xcb_grab_keyboard_reply_t * kr;

    int retry = 10000;

    while (retry--) {
        pc = xcb_grab_pointer(c, false, s->root, XCB_NONE,
                XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC,
                XCB_NONE, XCB_NONE, XCB_CURRENT_TIME);
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

static void lock(xcb_connection_t * c, lock_t * locks, const int ns) {
    // lock each screen, one by one
    const xcb_setup_t * xcb_setup = xcb_get_setup(c);
    xcb_screen_iterator_t iter  = xcb_setup_roots_iterator(xcb_setup);
    int i = 0;

   char color[] = "101010";

    // iterate through screens
    for (i = 0; i < ns; i++) {
        xcb_screen_t * s = iter.data;
        xcb_change_window_attributes(c, s->root, XCB_CW_EVENT_MASK,
                (uint32_t[]) { XCB_EVENT_MASK_STRUCTURE_NOTIFY });

        locks[i].lock_window = new_fullscreen_window(c, s, color);
        grab_everything_excpt_mediakey(c, s);
        xcb_screen_next(&iter);
    }
}

void idle_cb(const wtimer_t * t, const struct timeval * now) {
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
        case XK_Return:
        case XK_KP_Enter:
            pass_input[*pos] = 0;
            *pos = 0;
            ret = check_pass(pass_input, pass_sys)? pass_auth_fail
                                                  : pass_auth_succ;
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

#define Sec (1000 * 1000)
static void read_passwd(xcb_connection_t * c, const char * pass,
        const lock_t * locks, const int ns) {
    // init mainloop timers
    wtimer_t * idle_timer = wtimer_new(5 * Sec, idle_cb, WTIMER_ONESHOT);
    wtimer_list_t * tl = wtimer_list_new();
    wtimer_add(tl, idle_timer);

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
    char * pass_input = calloc(MAX_PASSLEN, sizeof(char));
    int  pass_pos = 0;
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
        nev = epoll_wait(epoll_fd, &ev, 1, to); // then we will wait

        // check and trigger timeouts
        ntimer = wtimer_list_timeout(tl, now);


        if (nev) { // we got xcb events
            xcb_generic_event_t * event;
            while ((event = xcb_poll_for_event(c))) {
                if (!event->response_type) goto next;
                int type = (event->response_type & 0x7f);
                int i = 0, ret = 0;

                switch (type) {
                case XCB_CIRCULATE_NOTIFY:
                    // this shouldn't be happening...
                    // unless some window sets itself on-top of the stack
                    for (i = 0; i < ns; i++)
                        set_window_ontop(c, locks[i].lock_window);
                    break;
                case XCB_KEY_PRESS:
                    ret = deal_with_key_press(
                            (xcb_key_press_event_t *)event, ksyms,
                            pass_input, &pass_pos, pass);

                    switch (ret) {
                        case pass_auth_succ: exit_now = 1; break;
                        case pass_auth_fail: // TODO: flash screen
                        case pass_not_check: // TODO: indicator
                        default:
                            break;
                    }

                    if (!pass_pos) dpms_off(c);

                    // reset timer
                    wtimer_rearm(idle_timer, 0, NULL);
                    break;
                }
next:
                free(event);
            }
        } else if (ntimer) { // we got timeouts
        } else {
            // why on earth we've been waken up???
        }
    }

    free(tl);
    free(idle_timer);
    // make sure this area of memory is wipped out
    clear_memory(pass_input, MAX_PASSLEN);
    free(pass_input);
    xcb_key_symbols_free(ksyms);
}

