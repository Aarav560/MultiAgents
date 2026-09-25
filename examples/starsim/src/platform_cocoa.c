/* platform_cocoa.c - macOS backend in pure C11 through the Objective-C runtime.
 *
 * No Objective-C source: every AppKit call is objc_msgSend cast to the exact function-pointer
 * type of the method (mandatory on arm64, where variadic and fixed-argument calls differ).
 * A custom NSView subclass ("StarsimView") and a window delegate ("StarsimWindowDelegate") are
 * built at runtime with objc_allocateClassPair + class_addMethod.
 *
 * Coordinates: the view is flipped, so its points run top-left like the framebuffer. The
 * reported client size and mouse positions are in points; on Retina screens Core Graphics
 * scales the image up to the backing pixels when we draw it into the view bounds.
 * Links with -framework Cocoa (which pulls in CoreGraphics and libobjc). */
#if defined(__APPLE__) && !defined(STARSIM_HEADLESS)

#define _DARWIN_C_SOURCE 1

#include <objc/runtime.h>
#include <objc/message.h>
#include <CoreGraphics/CoreGraphics.h>
#include <mach/mach_time.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "platform.h"

typedef CGRect NSRect;
typedef CGPoint NSPoint;
typedef CGSize NSSize;
typedef unsigned long NSUInteger;
typedef long NSInteger;

/* AppKit constants (values from the SDK headers). */
enum {
    NS_WINDOW_TITLED = 1 << 0,
    NS_WINDOW_CLOSABLE = 1 << 1,
    NS_WINDOW_MINIATURIZABLE = 1 << 2,
    NS_WINDOW_RESIZABLE = 1 << 3,
    NS_BACKING_BUFFERED = 2,
    NS_ACTIVATION_REGULAR = 0
};
#define NS_EVENT_MASK_ANY (~(unsigned long long)0)
#define NS_FLAG_SHIFT (1ul << 17)
#define NS_FLAG_CONTROL (1ul << 18)

/* Foundation's run loop mode constant (an NSString *), exported by Foundation. */
extern id const NSDefaultRunLoopMode;

/* ---- typed objc_msgSend wrappers ----
 * objc_msgSend is held in a void (*)(void) variable: casting from that type keeps
 * -Wcast-function-type quiet whatever prototype the SDK gives objc_msgSend (old variadic or new
 * void(void)), and gcc no longer flags "called through a non-compatible type". */
static void (*g_msgsend)(void) = (void (*)(void))objc_msgSend;
#define MSG_FN(type) ((type)g_msgsend)

static SEL sel(const char *name) { return sel_registerName(name); }
static id cls(const char *name) { return (id)objc_getClass(name); }

static id msg(id o, const char *s)
{
    return MSG_FN(id (*)(id, SEL))(o, sel(s));
}
static id msg_id(id o, const char *s, id a)
{
    return MSG_FN(id (*)(id, SEL, id))(o, sel(s), a);
}
static void msg_v(id o, const char *s)
{
    MSG_FN(void (*)(id, SEL))(o, sel(s));
}
static void msg_v_id(id o, const char *s, id a)
{
    MSG_FN(void (*)(id, SEL, id))(o, sel(s), a);
}
static void msg_v_bool(id o, const char *s, BOOL a)
{
    MSG_FN(void (*)(id, SEL, BOOL))(o, sel(s), a);
}
static void msg_v_long(id o, const char *s, NSInteger a)
{
    MSG_FN(void (*)(id, SEL, NSInteger))(o, sel(s), a);
}
static BOOL msg_bool(id o, const char *s)
{
    return MSG_FN(BOOL (*)(id, SEL))(o, sel(s));
}
static BOOL msg_bool_sel(id o, const char *s, SEL a)
{
    return MSG_FN(BOOL (*)(id, SEL, SEL))(o, sel(s), a);
}
static NSUInteger msg_uint(id o, const char *s)
{
    return MSG_FN(NSUInteger (*)(id, SEL))(o, sel(s));
}
static unsigned short msg_ushort(id o, const char *s)
{
    return MSG_FN(unsigned short (*)(id, SEL))(o, sel(s));
}
/* double comes back in a float register: plain objc_msgSend on x86_64 and arm64
   (objc_msgSend_fpret is only needed for long double on x86_64). */
static double msg_double(id o, const char *s)
{
    return MSG_FN(double (*)(id, SEL))(o, sel(s));
}
static const char *msg_cstr(id o, const char *s)
{
    return MSG_FN(const char *(*)(id, SEL))(o, sel(s));
}
/* NSPoint (two doubles) is returned in registers on both x86_64 and arm64. */
static NSPoint msg_point(id o, const char *s)
{
    return MSG_FN(NSPoint (*)(id, SEL))(o, sel(s));
}
static NSPoint msg_point_conv(id o, const char *s, NSPoint p, id view)
{
    return MSG_FN(NSPoint (*)(id, SEL, NSPoint, id))(o, sel(s), p, view);
}
/* NSRect (32 bytes) is returned through memory on x86_64, which needs objc_msgSend_stret;
   arm64 has no stret variant and returns it via plain objc_msgSend. */
static NSRect msg_rect(id o, const char *s)
{
#if defined(__x86_64__) || defined(__i386__)
    static void (*stret)(void) = (void (*)(void))objc_msgSend_stret;
    return ((NSRect (*)(id, SEL))stret)(o, sel(s));
#else
    return MSG_FN(NSRect (*)(id, SEL))(o, sel(s));
#endif
}

static id nsstring(const char *utf8)
{
    return MSG_FN(id (*)(id, SEL, const char *))(cls("NSString"), sel("stringWithUTF8String:"),
                                                 utf8 ? utf8 : "");
}

/* ---- backend state (one window) ---- */
static id g_app;
static id g_window;
static id g_view;
static id g_delegate;
static CGColorSpaceRef g_colorspace;
static pf_input g_in;          /* updated by the event methods, copied out by pf_poll */
static uint32_t *g_fb;         /* last presented frame, read by drawRect: */
static int g_fb_w, g_fb_h;
static size_t g_fb_cap;        /* capacity in pixels; grows only, so no per-frame malloc */

/* Wraps a block of AppKit calls in an NSAutoreleasePool so temporaries (NSEvent, NSString,
   NSDate) are freed every frame. Manual retain/release is fine: this file is not ARC. */
static id pool_push(void) { return msg(msg(cls("NSAutoreleasePool"), "alloc"), "init"); }
static void pool_pop(id pool) { msg_v(pool, "drain"); }

/* ---- event helpers ---- */

static void update_mouse_pos(id event)
{
    /* locationInWindow is in window base coordinates (origin bottom-left); converting into the
       flipped view gives top-left origin in points. */
    NSPoint p = msg_point(event, "locationInWindow");
    NSPoint v = msg_point_conv(g_view, "convertPoint:fromView:", p, (id)0);
    g_in.mouse_x = (int)v.x;
    g_in.mouse_y = (int)v.y;
}

static void mouse_button(id event, int button, int down)
{
    update_mouse_pos(event);
    if (down) {
        if (!g_in.mouse_down[button])
            g_in.mouse_pressed[button] = 1;
        g_in.mouse_down[button] = 1;
    } else {
        if (g_in.mouse_down[button])
            g_in.mouse_released[button] = 1;
        g_in.mouse_down[button] = 0;
    }
}

/* Maps an NSKeyDown/NSKeyUp event to a PF key, or -1. Special keys are matched by their
   hardware keyCode (layout independent); printable ones by their character, so '+' and '='
   follow the user's keyboard layout. */
static int map_key(id event)
{
    switch (msg_ushort(event, "keyCode")) {
    case 53:  return PF_KEY_ESCAPE;
    case 36:  return PF_KEY_ENTER;
    case 76:  return PF_KEY_ENTER;      /* keypad enter */
    case 48:  return PF_KEY_TAB;
    case 51:  return PF_KEY_BACKSPACE;  /* "delete" on Mac keyboards */
    case 123: return PF_KEY_LEFT;
    case 124: return PF_KEY_RIGHT;
    case 125: return PF_KEY_DOWN;
    case 126: return PF_KEY_UP;
    case 122: return PF_KEY_F1;
    case 69:  return '+';               /* keypad + */
    case 78:  return '-';               /* keypad - */
    default:  break;
    }
    /* charactersIgnoringModifiers drops ctrl/alt/cmd but keeps shift, so shift+'=' gives '+'. */
    id chars = msg(event, "charactersIgnoringModifiers");
    if (!chars)
        return -1;
    const char *s = msg_cstr(chars, "UTF8String");
    if (!s || !s[0] || (unsigned char)s[0] >= 128)
        return -1;
    int c = (unsigned char)s[0];
    if (c >= 'A' && c <= 'Z')
        c += 'a' - 'A';
    if (c == '\r')
        return PF_KEY_ENTER;
    if (c == 27)
        return PF_KEY_ESCAPE;
    return c;
}

/* ---- StarsimView methods ---- */

static BOOL view_accepts_first_mouse(id self, SEL cmd, id event)
{
    (void)self;
    (void)cmd;
    (void)event;
    return YES;   /* a click that activates the window also reaches the view */
}

static void view_draw_rect(id self, SEL cmd, NSRect dirty)
{
    (void)cmd;
    (void)dirty;
    if (!g_fb || g_fb_w <= 0 || g_fb_h <= 0)
        return;
    id gc = msg(cls("NSGraphicsContext"), "currentContext");
    if (!gc)
        return;
    /* -CGContext exists since 10.10; -graphicsPort is the older spelling of the same thing. */
    CGContextRef ctx = (CGContextRef)(msg_bool_sel(gc, "respondsToSelector:", sel("CGContext"))
                                          ? (void *)msg(gc, "CGContext")
                                          : (void *)msg(gc, "graphicsPort"));
    if (!ctx)
        return;

    size_t bytes = (size_t)g_fb_w * (size_t)g_fb_h * 4;
    CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, g_fb, bytes, NULL);
    /* 0x00RRGGBB words in little-endian memory = bytes B,G,R,X: 32-bit little-endian XRGB. */
    CGImageRef img = CGImageCreate((size_t)g_fb_w, (size_t)g_fb_h, 8, 32, (size_t)g_fb_w * 4,
                                   g_colorspace,
                                   (CGBitmapInfo)kCGImageAlphaNoneSkipFirst |
                                       kCGBitmapByteOrder32Little,
                                   prov, NULL, false, kCGRenderingIntentDefault);
    if (img) {
        NSRect b = msg_rect(self, "bounds");
        /* The view is flipped, so AppKit's CTM has y pointing down. CGContextDrawImage assumes
           y up, so flip back locally or the image comes out upside down. Drawing into the
           bounds (points) lets Core Graphics scale to the Retina backing store. */
        CGContextSaveGState(ctx);
        CGContextTranslateCTM(ctx, 0, b.size.height);
        CGContextScaleCTM(ctx, 1, -1);
        CGContextSetInterpolationQuality(ctx, kCGInterpolationLow);
        CGContextDrawImage(ctx, CGRectMake(0, 0, b.size.width, b.size.height), img);
        CGContextRestoreGState(ctx);
        CGImageRelease(img);
    }
    CGDataProviderRelease(prov);
}

static BOOL view_yes(id self, SEL cmd)
{
    (void)self;
    (void)cmd;
    return YES;
}

static void view_key_down(id self, SEL cmd, id event)
{
    (void)self;
    (void)cmd;
    int k = map_key(event);
    if (k >= 0 && k < PF_KEY_COUNT) {
        g_in.key_down[k] = 1;
        g_in.key_pressed[k] = 1;   /* auto-repeat events count as presses, per platform.h */
    }
}

static void view_key_up(id self, SEL cmd, id event)
{
    (void)self;
    (void)cmd;
    int k = map_key(event);
    if (k >= 0 && k < PF_KEY_COUNT)
        g_in.key_down[k] = 0;
}

/* Shift and control produce flagsChanged: rather than key events. */
static void view_flags_changed(id self, SEL cmd, id event)
{
    (void)self;
    (void)cmd;
    NSUInteger f = msg_uint(event, "modifierFlags");
    int shift = (f & NS_FLAG_SHIFT) != 0, ctrl = (f & NS_FLAG_CONTROL) != 0;
    if (shift && !g_in.key_down[PF_KEY_SHIFT])
        g_in.key_pressed[PF_KEY_SHIFT] = 1;
    if (ctrl && !g_in.key_down[PF_KEY_CTRL])
        g_in.key_pressed[PF_KEY_CTRL] = 1;
    g_in.key_down[PF_KEY_SHIFT] = (unsigned char)shift;
    g_in.key_down[PF_KEY_CTRL] = (unsigned char)ctrl;
}

static void view_mouse_down(id s, SEL c, id e) { (void)s; (void)c; mouse_button(e, PF_MOUSE_LEFT, 1); }
static void view_mouse_up(id s, SEL c, id e) { (void)s; (void)c; mouse_button(e, PF_MOUSE_LEFT, 0); }
static void view_rmouse_down(id s, SEL c, id e) { (void)s; (void)c; mouse_button(e, PF_MOUSE_RIGHT, 1); }
static void view_rmouse_up(id s, SEL c, id e) { (void)s; (void)c; mouse_button(e, PF_MOUSE_RIGHT, 0); }
static void view_omouse_down(id s, SEL c, id e) { (void)s; (void)c; mouse_button(e, PF_MOUSE_MIDDLE, 1); }
static void view_omouse_up(id s, SEL c, id e) { (void)s; (void)c; mouse_button(e, PF_MOUSE_MIDDLE, 0); }
static void view_mouse_moved(id s, SEL c, id e) { (void)s; (void)c; update_mouse_pos(e); }

static void view_scroll_wheel(id self, SEL cmd, id event)
{
    (void)self;
    (void)cmd;
    update_mouse_pos(event);
    /* deltaY is in "lines": about 1 per wheel notch (more with acceleration), fractional on
       trackpads. Positive = content scrolled up / wheel pushed away from the user. */
    g_in.wheel += msg_double(event, "deltaY");
}

/* ---- StarsimWindowDelegate ---- */

static BOOL delegate_should_close(id self, SEL cmd, id sender)
{
    (void)self;
    (void)cmd;
    (void)sender;
    g_in.quit = 1;
    return NO;   /* the app decides; pf_close tears the window down */
}

/* ---- class registration ---- */

#define ADD(cl, name, fn, types) class_addMethod(cl, sel(name), (IMP)(void (*)(void))(fn), types)

static Class view_class(void)
{
    Class c = objc_getClass("StarsimView");
    if (c)
        return c;   /* already registered by an earlier pf_open */
    c = objc_allocateClassPair(objc_getClass("NSView"), "StarsimView", 0);
    if (!c)
        return Nil;
    /* Type encodings: v void, c/B BOOL, @ id, : SEL, {CGRect=...} the NSRect argument. */
    ADD(c, "drawRect:", view_draw_rect, "v@:{CGRect={CGPoint=dd}{CGSize=dd}}");
    ADD(c, "acceptsFirstResponder", view_yes, "c@:");
    ADD(c, "isFlipped", view_yes, "c@:");
    ADD(c, "isOpaque", view_yes, "c@:");
    ADD(c, "acceptsFirstMouse:", view_accepts_first_mouse, "c@:@");
    ADD(c, "keyDown:", view_key_down, "v@:@");
    ADD(c, "keyUp:", view_key_up, "v@:@");
    ADD(c, "flagsChanged:", view_flags_changed, "v@:@");
    ADD(c, "mouseDown:", view_mouse_down, "v@:@");
    ADD(c, "mouseUp:", view_mouse_up, "v@:@");
    ADD(c, "mouseDragged:", view_mouse_moved, "v@:@");
    ADD(c, "mouseMoved:", view_mouse_moved, "v@:@");
    ADD(c, "rightMouseDown:", view_rmouse_down, "v@:@");
    ADD(c, "rightMouseUp:", view_rmouse_up, "v@:@");
    ADD(c, "rightMouseDragged:", view_mouse_moved, "v@:@");
    ADD(c, "otherMouseDown:", view_omouse_down, "v@:@");
    ADD(c, "otherMouseUp:", view_omouse_up, "v@:@");
    ADD(c, "otherMouseDragged:", view_mouse_moved, "v@:@");
    ADD(c, "scrollWheel:", view_scroll_wheel, "v@:@");
    objc_registerClassPair(c);
    return c;
}

static Class delegate_class(void)
{
    Class c = objc_getClass("StarsimWindowDelegate");
    if (c)
        return c;
    c = objc_allocateClassPair(objc_getClass("NSObject"), "StarsimWindowDelegate", 0);
    if (!c)
        return Nil;
    ADD(c, "windowShouldClose:", delegate_should_close, "c@:@");
    objc_registerClassPair(c);
    return c;
}

/* ---- public API ---- */

static void view_size(int *w, int *h)
{
    NSRect b = msg_rect(g_view, "bounds");
    *w = (int)(b.size.width + 0.5);
    *h = (int)(b.size.height + 0.5);
}

int pf_open(int w, int h, const char *title)
{
    if (g_window)
        return 0;
    id pool = pool_push();

    g_app = msg(cls("NSApplication"), "sharedApplication");
    if (!g_app) {
        pool_pop(pool);
        return -1;
    }
    /* A plain executable (no .app bundle) must ask to become a regular, dock-visible app or it
       never receives keyboard focus. */
    msg_v_long(g_app, "setActivationPolicy:", NS_ACTIVATION_REGULAR);
    msg_v(g_app, "finishLaunching");

    Class vc = view_class(), dc = delegate_class();
    if (!vc || !dc) {
        pool_pop(pool);
        return -1;
    }

    NSRect rect = CGRectMake(0, 0, w > 0 ? w : 1280, h > 0 ? h : 800);
    NSUInteger style = NS_WINDOW_TITLED | NS_WINDOW_CLOSABLE | NS_WINDOW_MINIATURIZABLE |
                       NS_WINDOW_RESIZABLE;
    id win = msg(cls("NSWindow"), "alloc");
    win = MSG_FN(id (*)(id, SEL, NSRect, NSUInteger, NSUInteger, BOOL))(
        win, sel("initWithContentRect:styleMask:backing:defer:"), rect, style,
        (NSUInteger)NS_BACKING_BUFFERED, NO);
    if (!win) {
        pool_pop(pool);
        return -1;
    }
    g_window = win;
    msg_v_bool(g_window, "setReleasedWhenClosed:", NO);   /* we release it in pf_close */

    g_view = MSG_FN(id (*)(id, SEL, NSRect))(msg((id)vc, "alloc"), sel("initWithFrame:"), rect);
    g_delegate = msg(msg((id)dc, "alloc"), "init");
    msg_v_id(g_window, "setContentView:", g_view);
    msg_v_id(g_window, "setDelegate:", g_delegate);
    msg_v_bool(g_window, "setAcceptsMouseMovedEvents:", YES);
    msg_v_id(g_window, "setTitle:", nsstring(title));
    msg_v(g_window, "center");
    msg_v_id(g_window, "makeKeyAndOrderFront:", (id)0);
    msg_id(g_window, "makeFirstResponder:", g_view);
    msg_v_bool(g_app, "activateIgnoringOtherApps:", YES);

    if (!g_colorspace)
        g_colorspace = CGColorSpaceCreateDeviceRGB();

    memset(&g_in, 0, sizeof g_in);
    view_size(&g_in.w, &g_in.h);
    g_in.resized = 1;

    pool_pop(pool);
    return 0;
}

int pf_poll(pf_input *in)
{
    g_in.resized = 0;
    g_in.wheel = 0;
    memset(g_in.mouse_pressed, 0, sizeof g_in.mouse_pressed);
    memset(g_in.mouse_released, 0, sizeof g_in.mouse_released);
    memset(g_in.key_pressed, 0, sizeof g_in.key_pressed);

    if (g_window) {
        id pool = pool_push();
        id past = msg(cls("NSDate"), "distantPast");   /* = do not block */
        for (;;) {
            id ev = MSG_FN(id (*)(id, SEL, unsigned long long, id, id, BOOL))(
                g_app, sel("nextEventMatchingMask:untilDate:inMode:dequeue:"), NS_EVENT_MASK_ANY,
                past, NSDefaultRunLoopMode, YES);
            if (!ev)
                break;
            /* sendEvent: routes to the key window's first responder (our view) and handles
               window dragging, resizing and the title-bar buttons. */
            msg_v_id(g_app, "sendEvent:", ev);
        }
        msg_v(g_app, "updateWindows");

        int w, h;
        view_size(&w, &h);
        if (w != g_in.w || h != g_in.h) {
            g_in.w = w;
            g_in.h = h;
            g_in.resized = 1;
        }
        /* If the window lost focus, key-up events went elsewhere: drop held keys. */
        if (!msg_bool(g_window, "isKeyWindow")) {
            memset(g_in.key_down, 0, sizeof g_in.key_down);
        }
        pool_pop(pool);
    }

    *in = g_in;
    return !g_in.quit;
}

void pf_present(const uint32_t *px, int w, int h)
{
    if (!g_window || !px || w <= 0 || h <= 0)
        return;
    size_t n = (size_t)w * (size_t)h;
    if (n > g_fb_cap) {
        uint32_t *nb = realloc(g_fb, n * sizeof *nb);
        if (!nb)
            return;
        g_fb = nb;
        g_fb_cap = n;
    }
    memcpy(g_fb, px, n * sizeof *g_fb);
    g_fb_w = w;
    g_fb_h = h;

    id pool = pool_push();
    msg_v_bool(g_view, "setNeedsDisplay:", YES);
    msg_v(g_view, "displayIfNeeded");   /* draws synchronously through drawRect: */
    pool_pop(pool);
}

void pf_set_title(const char *title)
{
    if (!g_window)
        return;
    id pool = pool_push();
    msg_v_id(g_window, "setTitle:", nsstring(title));
    pool_pop(pool);
}

void pf_close(void)
{
    if (!g_window)
        return;
    id pool = pool_push();
    msg_v_id(g_window, "setDelegate:", (id)0);
    msg_v(g_window, "close");
    msg_v(g_window, "release");
    msg_v(g_view, "release");
    msg_v(g_delegate, "release");
    pool_pop(pool);
    g_window = g_view = g_delegate = (id)0;
    free(g_fb);
    g_fb = NULL;
    g_fb_cap = 0;
    g_fb_w = g_fb_h = 0;
    if (g_colorspace) {
        CGColorSpaceRelease(g_colorspace);
        g_colorspace = NULL;
    }
}

double pf_time(void)
{
    static mach_timebase_info_data_t tb;
    static uint64_t t0;
    if (tb.denom == 0) {
        mach_timebase_info(&tb);
        t0 = mach_absolute_time();
    }
    uint64_t dt = mach_absolute_time() - t0;
    return (double)dt * (double)tb.numer / (double)tb.denom * 1e-9;
}

void pf_sleep(double seconds)
{
    if (seconds <= 0)
        return;
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
        /* interrupted by a signal: sleep the remainder */
    }
}

#else
/* ISO C forbids an empty translation unit. */
typedef int starsim_platform_cocoa_unused;
#endif
