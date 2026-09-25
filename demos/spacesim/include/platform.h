/* platform.h - the few OS services orbit needs, for POSIX (Linux, macOS) and Windows. */
#ifndef ORBIT_PLATFORM_H
#define ORBIT_PLATFORM_H

/* Monotonic wall clock in seconds (arbitrary origin). */
double plat_now(void);
/* Sleeps for the given number of seconds (no-op when <= 0). */
void plat_sleep(double seconds);
/* Creates one directory. Returns 0 on success or if it already exists, -1 otherwise. */
int plat_mkdir(const char *path);
/* Returns 1 if path is an existing directory. */
int plat_is_dir(const char *path);
/* Turns on ANSI escape handling in the Windows console; no-op elsewhere. */
void plat_enable_ansi(void);

#endif
