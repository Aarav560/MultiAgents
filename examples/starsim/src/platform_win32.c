/* platform_win32.c - Win32 + GDI backend (links user32, gdi32; winmm is loaded dynamically). */
#if defined(_WIN32) && !defined(STARSIM_HEADLESS)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string.h>

#include "platform.h"

#ifndef WHEEL_DELTA
#define WHEEL_DELTA 120
#endif
#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(wp) ((short)HIWORD(wp))
#endif

typedef BOOL (WINAPI *set_dpi_aware_fn)(void);
typedef UINT (WINAPI *time_period_fn)(UINT);

static const char *const class_name = "starsim_window";

static HWND g_hwnd;
static HDC g_hdc;
static pf_input g_in;
static int g_buttons_held;
static HMODULE g_winmm;
static time_period_fn g_time_end;
static int g_timer_raised;

static LARGE_INTEGER g_qpc_freq;
static LARGE_INTEGER g_qpc_start;
static int g_qpc_ready;

static int map_key(WPARAM vk)
{
    if (vk >= 'A' && vk <= 'Z') return (int)(vk - 'A' + 'a');
    if (vk >= '0' && vk <= '9') return (int)vk;
    if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) return (int)(vk - VK_NUMPAD0 + '0');
    switch (vk) {
    case VK_SPACE: return ' ';
    case VK_OEM_PLUS: case VK_ADD: return '+';
    case VK_OEM_MINUS: case VK_SUBTRACT: return '-';
    case VK_OEM_4: return '[';
    case VK_OEM_6: return ']';
    case VK_OEM_COMMA: return ',';
    case VK_OEM_PERIOD: case VK_DECIMAL: return '.';
    case VK_OEM_2: case VK_DIVIDE: return '/';
    case VK_ESCAPE: return PF_KEY_ESCAPE;
    case VK_RETURN: return PF_KEY_ENTER;
    case VK_TAB: return PF_KEY_TAB;
    case VK_BACK: return PF_KEY_BACKSPACE;
    case VK_LEFT: return PF_KEY_LEFT;
    case VK_RIGHT: return PF_KEY_RIGHT;
    case VK_UP: return PF_KEY_UP;
    case VK_DOWN: return PF_KEY_DOWN;
    case VK_F1: return PF_KEY_F1;
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return PF_KEY_SHIFT;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return PF_KEY_CTRL;
    default: return -1;
    }
}

static void set_mouse_pos(LPARAM lp)
{
    g_in.mouse_x = (int)(short)LOWORD(lp);
    g_in.mouse_y = (int)(short)HIWORD(lp);
}

static void mouse_button(HWND hwnd, int b, int down, LPARAM lp)
{
    set_mouse_pos(lp);
    if (down) {
        if (!g_in.mouse_down[b]) g_in.mouse_pressed[b] = 1;
        g_in.mouse_down[b] = 1;
        if (g_buttons_held++ == 0) SetCapture(hwnd);
    } else {
        if (g_in.mouse_down[b]) g_in.mouse_released[b] = 1;
        g_in.mouse_down[b] = 0;
        if (g_buttons_held > 0 && --g_buttons_held == 0) ReleaseCapture();
    }
}

static void release_all(void)
{
    int b;
    for (b = 0; b < 3; b++) {
        if (g_in.mouse_down[b]) g_in.mouse_released[b] = 1;
        g_in.mouse_down[b] = 0;
    }
    g_buttons_held = 0;
    memset(g_in.key_down, 0, sizeof g_in.key_down);
}

static void key_event(WPARAM vk, int down)
{
    int k = map_key(vk);
    if (k < 0 || k >= PF_KEY_COUNT) return;
    if (down) g_in.key_pressed[k] = 1;
    g_in.key_down[k] = (unsigned char)(down != 0);
}

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CLOSE:
        g_in.quit = 1;
        return 0;
    case WM_DESTROY:
        return 0;
    case WM_SIZE:
        if (wp != SIZE_MINIMIZED) {
            int w = (int)LOWORD(lp), h = (int)HIWORD(lp);
            if (w > 0 && h > 0 && (w != g_in.w || h != g_in.h)) {
                g_in.w = w;
                g_in.h = h;
                g_in.resized = 1;
            }
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        key_event(wp, 1);
        if (msg == WM_SYSKEYDOWN) break;
        return 0;
    case WM_KEYUP: case WM_SYSKEYUP:
        key_event(wp, 0);
        if (msg == WM_SYSKEYUP) break;
        return 0;
    case WM_KILLFOCUS:
        release_all();
        return 0;
    case WM_CAPTURECHANGED:
        g_buttons_held = 0;
        return 0;
    case WM_MOUSEMOVE:
        set_mouse_pos(lp);
        return 0;
    case WM_LBUTTONDOWN: mouse_button(hwnd, PF_MOUSE_LEFT, 1, lp); return 0;
    case WM_LBUTTONUP: mouse_button(hwnd, PF_MOUSE_LEFT, 0, lp); return 0;
    case WM_RBUTTONDOWN: mouse_button(hwnd, PF_MOUSE_RIGHT, 1, lp); return 0;
    case WM_RBUTTONUP: mouse_button(hwnd, PF_MOUSE_RIGHT, 0, lp); return 0;
    case WM_MBUTTONDOWN: mouse_button(hwnd, PF_MOUSE_MIDDLE, 1, lp); return 0;
    case WM_MBUTTONUP: mouse_button(hwnd, PF_MOUSE_MIDDLE, 0, lp); return 0;
    case WM_MOUSEWHEEL:
        g_in.wheel += (double)GET_WHEEL_DELTA_WPARAM(wp) / (double)WHEEL_DELTA;
        return 0;
    default:
        break;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void make_dpi_aware(void)
{
    HMODULE user32 = GetModuleHandleA("user32.dll");
    FARPROC p;
    if (!user32) return;
    p = GetProcAddress(user32, "SetProcessDPIAware");
    if (p) ((set_dpi_aware_fn)(void (*)(void))p)();
}

static void raise_timer_resolution(void)
{
    FARPROC begin, end;
    if (g_timer_raised) return;
    g_timer_raised = 1;
    g_winmm = LoadLibraryA("winmm.dll");
    if (!g_winmm) return;
    begin = GetProcAddress(g_winmm, "timeBeginPeriod");
    end = GetProcAddress(g_winmm, "timeEndPeriod");
    if (begin && end) {
        ((time_period_fn)(void (*)(void))begin)(1);
        g_time_end = (time_period_fn)(void (*)(void))end;
    }
}

int pf_open(int w, int h, const char *title)
{
    HINSTANCE inst = GetModuleHandleA(NULL);
    WNDCLASSA wc;
    RECT r;
    DWORD style = WS_OVERLAPPEDWINDOW;

    if (g_hwnd) return 0;
    if (w <= 0 || h <= 0) return -1;
    make_dpi_aware();

    memset(&wc, 0, sizeof wc);
    wc.style = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = wnd_proc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wc.lpszClassName = class_name;
    if (!RegisterClassA(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return -1;

    r.left = 0;
    r.top = 0;
    r.right = w;
    r.bottom = h;
    AdjustWindowRect(&r, style, FALSE);

    memset(&g_in, 0, sizeof g_in);
    g_in.w = w;
    g_in.h = h;
    g_buttons_held = 0;

    g_hwnd = CreateWindowExA(0, class_name, title ? title : "starsim", style,
                             CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                             NULL, NULL, inst, NULL);
    if (!g_hwnd) return -1;
    g_hdc = GetDC(g_hwnd);
    if (!g_hdc) {
        DestroyWindow(g_hwnd);
        g_hwnd = NULL;
        return -1;
    }
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    SetForegroundWindow(g_hwnd);

    /* The client size may differ from the request (screen too small); report the real one. */
    if (GetClientRect(g_hwnd, &r) && r.right > 0 && r.bottom > 0) {
        g_in.w = r.right;
        g_in.h = r.bottom;
    }
    g_in.resized = 0;
    raise_timer_resolution();
    return 0;
}

int pf_poll(pf_input *in)
{
    MSG msg;

    memset(g_in.mouse_pressed, 0, sizeof g_in.mouse_pressed);
    memset(g_in.mouse_released, 0, sizeof g_in.mouse_released);
    memset(g_in.key_pressed, 0, sizeof g_in.key_pressed);
    g_in.wheel = 0.0;
    g_in.resized = 0;

    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            g_in.quit = 1;
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    if (!g_hwnd) g_in.quit = 1;
    if (in) *in = g_in;
    return g_in.quit ? 0 : 1;
}

void pf_present(const uint32_t *px, int w, int h)
{
    BITMAPINFO bmi;
    RECT r;
    int cw, ch;

    if (!g_hdc || !px || w <= 0 || h <= 0) return;
    if (!GetClientRect(g_hwnd, &r)) return;
    cw = r.right - r.left;
    ch = r.bottom - r.top;
    if (cw <= 0 || ch <= 0) return;

    memset(&bmi, 0, sizeof bmi);
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h; /* negative: top-down rows */
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetStretchBltMode(g_hdc, COLORONCOLOR);
    StretchDIBits(g_hdc, 0, 0, cw, ch, 0, 0, w, h, px, &bmi, DIB_RGB_COLORS, SRCCOPY);
}

void pf_set_title(const char *title)
{
    if (g_hwnd && title) SetWindowTextA(g_hwnd, title);
}

void pf_close(void)
{
    if (g_hwnd) {
        if (g_hdc) ReleaseDC(g_hwnd, g_hdc);
        DestroyWindow(g_hwnd);
        UnregisterClassA(class_name, GetModuleHandleA(NULL));
    }
    g_hdc = NULL;
    g_hwnd = NULL;
    g_buttons_held = 0;
    if (g_time_end) g_time_end(1);
    g_time_end = NULL;
    if (g_winmm) FreeLibrary(g_winmm);
    g_winmm = NULL;
    g_timer_raised = 0;
}

double pf_time(void)
{
    LARGE_INTEGER now;
    if (!g_qpc_ready) {
        QueryPerformanceFrequency(&g_qpc_freq);
        QueryPerformanceCounter(&g_qpc_start);
        if (g_qpc_freq.QuadPart <= 0) g_qpc_freq.QuadPart = 1;
        g_qpc_ready = 1;
    }
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - g_qpc_start.QuadPart) / (double)g_qpc_freq.QuadPart;
}

void pf_sleep(double seconds)
{
    DWORD ms;
    if (seconds <= 0.0) return;
    raise_timer_resolution();
    ms = (DWORD)(seconds * 1000.0 + 0.5);
    Sleep(ms);
}

#else
typedef int starsim_platform_win32_unused;
#endif

/* Keeps ISO C happy when the #if above excludes this whole file (empty translation unit). */
typedef int platform_win32_unused;
