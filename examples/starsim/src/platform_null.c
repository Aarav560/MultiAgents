#if defined(STARSIM_HEADLESS)

#ifdef _WIN32
    #include <windows.h>
#else
    #define _POSIX_C_SOURCE 200809L
    #include <time.h>
#endif

#include "platform.h"

int pf_open(int w, int h, const char *title) {
    (void)w;
    (void)h;
    (void)title;
    return -1;
}

int pf_poll(pf_input *in) {
    in->resized = 0;
    in->wheel = 0.0;
    for (int i = 0; i < 3; i++) {
        in->mouse_pressed[i] = 0;
        in->mouse_released[i] = 0;
    }
    for (int i = 0; i < PF_KEY_COUNT; i++) {
        in->key_pressed[i] = 0;
    }
    in->quit = 1;
    return 0;
}

void pf_present(const uint32_t *px, int w, int h) {
    (void)px;
    (void)w;
    (void)h;
}

void pf_set_title(const char *title) {
    (void)title;
}

void pf_close(void) {
}

#ifdef _WIN32

static LARGE_INTEGER freq;
static int freq_init;

double pf_time(void) {
    if (!freq_init) {
        QueryPerformanceFrequency(&freq);
        freq_init = 1;
    }
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart / (double)freq.QuadPart;
}

void pf_sleep(double seconds) {
    Sleep((DWORD)(seconds * 1000.0));
}

#else

double pf_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void pf_sleep(double seconds) {
    struct timespec ts;
    ts.tv_sec = (time_t)seconds;
    ts.tv_nsec = (long)((seconds - (double)ts.tv_sec) * 1e9);
    nanosleep(&ts, NULL);
}

#endif

#endif

/* Keeps ISO C happy when the #if above excludes this whole file (empty translation unit). */
typedef int platform_null_unused;
