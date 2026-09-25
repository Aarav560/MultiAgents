/* platform_x11.c - Xlib backend for platform.h. No XShm, no extensions: XCreateSimpleWindow,
 * a WM_DELETE_WINDOW protocol for close, and an XImage over a malloc'd buffer that is recreated
 * on resize. pf_present nearest-neighbour stretches into that buffer when sizes differ. */
#if !defined(STARSIM_HEADLESS) && !defined(_WIN32) && !defined(__APPLE__)

#define _POSIX_C_SOURCE 200809L

#include "platform.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

static Display *g_dpy = NULL;
static int g_screen = 0;
static Window g_win = 0;
static GC g_gc = 0;
static Atom g_wm_delete = 0;
static Visual *g_visual = NULL;
static int g_depth = 0;

static XImage *g_img = NULL;
static uint32_t *g_img_buf = NULL; /* backing store for g_img, 32-bit BGRX per pixel */
static int g_img_w = 0, g_img_h = 0;

static int g_win_w = 0, g_win_h = 0;

static int pf_key_from_keysym(KeySym ks) {
    if (ks >= XK_a && ks <= XK_z) return (int)(ks - XK_a) + 'a';
    if (ks >= XK_A && ks <= XK_Z) return (int)(ks - XK_A) + 'a';
    if (ks >= XK_0 && ks <= XK_9) return (int)(ks - XK_0) + '0';
    switch (ks) {
        case XK_space: return ' ';
        case XK_plus: return '+';
        case XK_equal: return '=';
        case XK_minus: return '-';
        case XK_comma: return ',';
        case XK_period: return '.';
        case XK_slash: return '/';
        case XK_bracketleft: return '[';
        case XK_bracketright: return ']';
        case XK_Escape: return PF_KEY_ESCAPE;
        case XK_Return: return PF_KEY_ENTER;
        case XK_KP_Enter: return PF_KEY_ENTER;
        case XK_Tab: return PF_KEY_TAB;
        case XK_BackSpace: return PF_KEY_BACKSPACE;
        case XK_Left: return PF_KEY_LEFT;
        case XK_Right: return PF_KEY_RIGHT;
        case XK_Up: return PF_KEY_UP;
        case XK_Down: return PF_KEY_DOWN;
        case XK_F1: return PF_KEY_F1;
        case XK_Shift_L: return PF_KEY_SHIFT;
        case XK_Shift_R: return PF_KEY_SHIFT;
        case XK_Control_L: return PF_KEY_CTRL;
        case XK_Control_R: return PF_KEY_CTRL;
        default: return -1;
    }
}

static void pf_free_image(void) {
    if (g_img) {
        /* XDestroyImage frees the data pointer it was given; since that pointer is our
           g_img_buf, null it out here and let XDestroyImage release the memory. */
        g_img->data = NULL;
        XDestroyImage(g_img);
        g_img = NULL;
    }
    free(g_img_buf);
    g_img_buf = NULL;
    g_img_w = g_img_h = 0;
}

static int pf_alloc_image(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    pf_free_image();
    g_img_buf = (uint32_t *)malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!g_img_buf) return -1;
    memset(g_img_buf, 0, (size_t)w * (size_t)h * sizeof(uint32_t));
    g_img = XCreateImage(g_dpy, g_visual, (unsigned)g_depth, ZPixmap, 0,
                          (char *)g_img_buf, (unsigned)w, (unsigned)h, 32, 0);
    if (!g_img) {
        free(g_img_buf);
        g_img_buf = NULL;
        return -1;
    }
    g_img_w = w;
    g_img_h = h;
    return 0;
}

int pf_open(int w, int h, const char *title) {
    g_dpy = XOpenDisplay(NULL);
    if (!g_dpy) return -1;

    g_screen = DefaultScreen(g_dpy);
    g_depth = DefaultDepth(g_dpy, g_screen);
    g_visual = DefaultVisual(g_dpy, g_screen);

    if (g_depth != 24 && g_depth != 32) {
        XCloseDisplay(g_dpy);
        g_dpy = NULL;
        return -1;
    }
    if (g_visual->class != TrueColor) {
        XCloseDisplay(g_dpy);
        g_dpy = NULL;
        return -1;
    }

    Window root = RootWindow(g_dpy, g_screen);
    unsigned long black = BlackPixel(g_dpy, g_screen);
    unsigned long white = WhitePixel(g_dpy, g_screen);

    g_win = XCreateSimpleWindow(g_dpy, root, 0, 0, (unsigned)w, (unsigned)h, 0, white, black);
    if (!g_win) {
        XCloseDisplay(g_dpy);
        g_dpy = NULL;
        return -1;
    }

    XStoreName(g_dpy, g_win, title ? title : "starsim");

    XSelectInput(g_dpy, g_win,
                 ExposureMask | StructureNotifyMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask | PointerMotionMask);

    g_wm_delete = XInternAtom(g_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_dpy, g_win, &g_wm_delete, 1);

    XSizeHints *hints = XAllocSizeHints();
    if (hints) {
        hints->flags = PMinSize;
        hints->min_width = 1;
        hints->min_height = 1;
        XSetWMNormalHints(g_dpy, g_win, hints);
        XFree(hints);
    }

    g_gc = XCreateGC(g_dpy, g_win, 0, NULL);

    XMapWindow(g_dpy, g_win);
    XFlush(g_dpy);

    g_win_w = w;
    g_win_h = h;

    if (pf_alloc_image(w, h) != 0) {
        XFreeGC(g_dpy, g_gc);
        XDestroyWindow(g_dpy, g_win);
        XCloseDisplay(g_dpy);
        g_dpy = NULL;
        g_win = 0;
        return -1;
    }

    return 0;
}

int pf_poll(pf_input *in) {
    in->resized = 0;
    in->wheel = 0.0;
    memset(in->mouse_pressed, 0, sizeof(in->mouse_pressed));
    memset(in->mouse_released, 0, sizeof(in->mouse_released));
    memset(in->key_pressed, 0, sizeof(in->key_pressed));

    if (!g_dpy) {
        in->quit = 1;
        return 0;
    }

    while (XPending(g_dpy) > 0) {
        XEvent ev;
        XNextEvent(g_dpy, &ev);
        switch (ev.type) {
            case ClientMessage:
                if ((Atom)ev.xclient.data.l[0] == g_wm_delete) {
                    in->quit = 1;
                }
                break;
            case ConfigureNotify:
                if (ev.xconfigure.width != g_win_w || ev.xconfigure.height != g_win_h) {
                    g_win_w = ev.xconfigure.width;
                    g_win_h = ev.xconfigure.height;
                    in->w = g_win_w;
                    in->h = g_win_h;
                    in->resized = 1;
                }
                break;
            case MotionNotify:
                in->mouse_x = ev.xmotion.x;
                in->mouse_y = ev.xmotion.y;
                break;
            case ButtonPress: {
                int btn = -1;
                if (ev.xbutton.button == Button1) btn = PF_MOUSE_LEFT;
                else if (ev.xbutton.button == Button3) btn = PF_MOUSE_RIGHT;
                else if (ev.xbutton.button == Button2) btn = PF_MOUSE_MIDDLE;
                else if (ev.xbutton.button == Button4) in->wheel += 1.0;
                else if (ev.xbutton.button == Button5) in->wheel -= 1.0;
                if (btn >= 0) {
                    in->mouse_down[btn] = 1;
                    in->mouse_pressed[btn] = 1;
                }
                in->mouse_x = ev.xbutton.x;
                in->mouse_y = ev.xbutton.y;
                break;
            }
            case ButtonRelease: {
                int btn = -1;
                if (ev.xbutton.button == Button1) btn = PF_MOUSE_LEFT;
                else if (ev.xbutton.button == Button3) btn = PF_MOUSE_RIGHT;
                else if (ev.xbutton.button == Button2) btn = PF_MOUSE_MIDDLE;
                if (btn >= 0) {
                    in->mouse_down[btn] = 0;
                    in->mouse_released[btn] = 1;
                }
                in->mouse_x = ev.xbutton.x;
                in->mouse_y = ev.xbutton.y;
                break;
            }
            case KeyPress: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                int code = pf_key_from_keysym(ks);
                if (code >= 0 && code < PF_KEY_COUNT) {
                    in->key_down[code] = 1;
                    in->key_pressed[code] = 1;
                }
                break;
            }
            case KeyRelease: {
                KeySym ks = XLookupKeysym(&ev.xkey, 0);
                int code = pf_key_from_keysym(ks);
                if (code >= 0 && code < PF_KEY_COUNT) {
                    in->key_down[code] = 0;
                }
                break;
            }
            default:
                break;
        }
    }

    return in->quit ? 0 : 1;
}

void pf_present(const uint32_t *px, int w, int h) {
    if (!g_dpy || !g_img) return;
    if (w < 1 || h < 1) return;

    if (w != g_img_w || h != g_img_h) {
        if (w == g_win_w && h == g_win_h) {
            /* framebuffer already matches window size: swap the backing buffer directly */
            if (pf_alloc_image(w, h) != 0) return;
        }
    }

    if (w == g_img_w && h == g_img_h) {
        memcpy(g_img_buf, px, (size_t)w * (size_t)h * sizeof(uint32_t));
    } else {
        /* nearest-neighbour stretch from the w x h framebuffer into the g_img_w x g_img_h buffer */
        if (pf_alloc_image(g_win_w, g_win_h) != 0) return;
        for (int y = 0; y < g_img_h; y++) {
            int sy = (int)((int64_t)y * h / g_img_h);
            if (sy >= h) sy = h - 1;
            const uint32_t *srow = px + (size_t)sy * (size_t)w;
            uint32_t *drow = g_img_buf + (size_t)y * (size_t)g_img_w;
            for (int x = 0; x < g_img_w; x++) {
                int sx = (int)((int64_t)x * w / g_img_w);
                if (sx >= w) sx = w - 1;
                drow[x] = srow[sx];
            }
        }
    }

    XPutImage(g_dpy, g_win, g_gc, g_img, 0, 0, 0, 0, (unsigned)g_img_w, (unsigned)g_img_h);
    XFlush(g_dpy);
}

void pf_set_title(const char *title) {
    if (!g_dpy) return;
    XStoreName(g_dpy, g_win, title ? title : "starsim");
    XFlush(g_dpy);
}

void pf_close(void) {
    if (!g_dpy) return;
    pf_free_image();
    if (g_gc) {
        XFreeGC(g_dpy, g_gc);
        g_gc = 0;
    }
    if (g_win) {
        XDestroyWindow(g_dpy, g_win);
        g_win = 0;
    }
    XCloseDisplay(g_dpy);
    g_dpy = NULL;
}

double pf_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

void pf_sleep(double seconds) {
    if (seconds <= 0.0) return;
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

#endif /* !STARSIM_HEADLESS && !_WIN32 && !__APPLE__ */
