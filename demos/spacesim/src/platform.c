#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "platform.h"

#ifdef _WIN32

#include <direct.h>
#include <errno.h>
#include <sys/stat.h>
#include <windows.h>

double plat_now(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (double)c.QuadPart / (double)f.QuadPart;
}

void plat_sleep(double seconds) {
    if (seconds > 0.0) Sleep((DWORD)(seconds * 1000.0 + 0.5));
}

int plat_mkdir(const char *path) {
    if (_mkdir(path) == 0 || errno == EEXIST) return 0;
    return -1;
}

int plat_is_dir(const char *path) {
    struct _stat st;
    return _stat(path, &st) == 0 && (st.st_mode & _S_IFDIR);
}

void plat_enable_ansi(void) {
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode))
        SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
}

#else

#include <errno.h>
#include <sys/stat.h>
#include <time.h>

double plat_now(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

void plat_sleep(double seconds) {
    if (seconds <= 0.0) return;
    struct timespec t;
    t.tv_sec = (time_t)seconds;
    t.tv_nsec = (long)((seconds - (double)t.tv_sec) * 1e9);
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {
    }
}

int plat_mkdir(const char *path) {
    if (mkdir(path, 0755) == 0 || errno == EEXIST) return 0;
    return -1;
}

int plat_is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

void plat_enable_ansi(void) {}

#endif
