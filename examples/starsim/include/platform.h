/* platform.h - window, input, timing. One backend is compiled per OS:
 *   src/platform_win32.c  (_WIN32, links user32 gdi32)
 *   src/platform_cocoa.c  (__APPLE__, links -framework Cocoa, pure C via the Objective-C runtime)
 *   src/platform_x11.c    (other Unix, links -lX11)
 *   src/platform_null.c   (when STARSIM_HEADLESS is defined: no window; for servers and CI)
 * Each file wraps its whole body in the matching #if so all four can always be compiled. */
#ifndef STARSIM_PLATFORM_H
#define STARSIM_PLATFORM_H

#include <stdint.h>

/* Key codes: printable keys use their lowercase ASCII value ('a'..'z', '0'..'9', ' ', '+', '-',
   '=', ',', '.', '/'). Special keys: */
enum {
    PF_KEY_ESCAPE = 128,
    PF_KEY_ENTER,
    PF_KEY_TAB,
    PF_KEY_BACKSPACE,
    PF_KEY_LEFT,
    PF_KEY_RIGHT,
    PF_KEY_UP,
    PF_KEY_DOWN,
    PF_KEY_F1,
    PF_KEY_SHIFT,
    PF_KEY_CTRL,
    PF_KEY_COUNT = 256
};

enum { PF_MOUSE_LEFT = 0, PF_MOUSE_RIGHT = 1, PF_MOUSE_MIDDLE = 2 };

typedef struct {
    int quit;               /* window close requested */
    int w, h;               /* current client size in pixels */
    int resized;            /* 1 on the poll where the size changed */
    int mouse_x, mouse_y;   /* client coordinates, origin top-left */
    int mouse_down[3];
    int mouse_pressed[3];   /* went down during the last poll */
    int mouse_released[3];  /* went up during the last poll */
    double wheel;           /* wheel notches during the last poll, + = away from user (zoom in) */
    unsigned char key_down[PF_KEY_COUNT];
    unsigned char key_pressed[PF_KEY_COUNT]; /* went down during the last poll (auto-repeat allowed) */
} pf_input;

/* Opens a resizable window with a w x h client area. 0 ok, -1 failure (e.g. no display). */
int pf_open(int w, int h, const char *title);
/* Clears the per-poll fields (pressed, released, wheel, resized) then drains pending events
   without blocking. Returns 0 once quit is requested, else 1. */
int pf_poll(pf_input *in);
/* Shows a w x h framebuffer (0x00RRGGBB, top row first), stretched to the client area if sizes differ. */
void pf_present(const uint32_t *px, int w, int h);
void pf_set_title(const char *title);
void pf_close(void);
/* Monotonic seconds, and sleep. Work without pf_open (headless mode uses them). */
double pf_time(void);
void pf_sleep(double seconds);

#endif
