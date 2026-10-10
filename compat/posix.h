#ifndef POSIX_H
#define POSIX_H

/* ------------------------------------------------------------------
 *  Windows compatibility layer.
 *
 *  The rest of the codebase is written against POSIX. This header
 *  provides just enough to compile on MSVC: the type ssize_t, the
 *  unistd.h accessors, strtok_r, localtime_r, usleep, S_ISREG, and
 *  the signal constants MSVC doesn't define.
 *
 *  On POSIX builds this header is a no-op.
 * ------------------------------------------------------------------ */

#if defined(_WIN32)

/* _WINSOCKAPI_ tells <windows.h> that winsock.h has already been
 * included, so it skips the legacy winsock.h. mongoose.h includes
 * winsock2.h, and the two headers cannot coexist in one TU. This
 * must be set before the very first system header of the file. */
#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

/* CINTERFACE must be in effect before <windows.h> is included, or
 * ... (existing comment) ... */
#ifndef CINTERFACE
#define CINTERFACE
#endif

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <io.h>
#include <process.h>
#include <direct.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <windows.h>
#include <stdio.h>

/* ---- types ---- */
#ifndef _SSIZE_T_DEFINED
  typedef long long ssize_t;
  #define _SSIZE_T_DEFINED
#endif

/* ---- file descriptor constants ---- */
#ifndef STDOUT_FILENO
  #define STDOUT_FILENO 1
#endif
#ifndef STDIN_FILENO
  #define STDIN_FILENO 0
#endif
#ifndef STDERR_FILENO
  #define STDERR_FILENO 2
#endif

/* ---- POSIX names for MSVC CRT functions ---- */
#ifndef read
  #define read  _read
#endif
#ifndef write
  #define write _write
#endif
#ifndef close
  #define close _close
#endif
#ifndef unlink
  #define unlink _unlink
#endif
#ifndef getcwd
  #define getcwd _getcwd
#endif
#ifndef mkdir
  #define mkdir(p, m) _mkdir(p)
#endif
#ifndef rmdir
  #define rmdir _rmdir
#endif
#ifndef strdup
  #define strdup _strdup
#endif
#ifndef strcasecmp
  #define strcasecmp _stricmp
#endif
#ifndef strncasecmp
  #define strncasecmp _strnicmp
#endif
#ifndef fileno
  #define fileno _fileno
#endif

/* ---- strtok_r ---- */
static inline char *wisp_strtok_r(char *s, const char *delim, char **save) {
    return strtok_s(s, delim, save);
}
#define strtok_r wisp_strtok_r

/* ---- localtime_r ---- */
static inline struct tm *wisp_localtime_r(const time_t *t, struct tm *out) {
    if (localtime_s(out, t) != 0) return NULL;
    return out;
}
#define localtime_r wisp_localtime_r

/* ---- usleep ---- */
static inline int wisp_usleep(unsigned int usec) {
    Sleep(usec / 1000);
    return 0;
}
#define usleep wisp_usleep

/* ---- clock_gettime ---- */
#ifndef CLOCK_MONOTONIC
  #define CLOCK_MONOTONIC 1
#endif
#ifndef CLOCK_REALTIME
  #define CLOCK_REALTIME 0
#endif

struct wisp_timespec { long tv_sec; long tv_nsec; };

static inline int wisp_clock_gettime(int clk, struct timespec *ts) {
    if (clk == CLOCK_MONOTONIC) {
        static LARGE_INTEGER s_freq = {0};
        LARGE_INTEGER now;
        if (s_freq.QuadPart == 0) QueryPerformanceFrequency(&s_freq);
        QueryPerformanceCounter(&now);
        ts->tv_sec  = (time_t)(now.QuadPart / s_freq.QuadPart);
        ts->tv_nsec = (long)(((now.QuadPart % s_freq.QuadPart) * 1000000000LL)
                             / s_freq.QuadPart);
        return 0;
    }
    /* CLOCK_REALTIME */
    {
        FILETIME ft;
        ULARGE_INTEGER u;
        GetSystemTimeAsFileTime(&ft);
        u.LowPart  = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        /* 100-ns intervals since 1601-01-01. Convert to Unix epoch. */
        unsigned long long ns100 = u.QuadPart - 116444736000000000ULL;
        ts->tv_sec  = (time_t)(ns100 / 10000000ULL);
        ts->tv_nsec = (long)((ns100 % 10000000ULL) * 100);
        return 0;
    }
}
#define clock_gettime(clk, ts) wisp_clock_gettime((clk), (ts))

/* ---- getline ---- */
static inline ssize_t wisp_getline(char **lineptr, size_t *n, FILE *stream) {
    if (!lineptr || !n || !stream) return -1;
    if (*lineptr == NULL || *n == 0) {
        *n = 256;
        *lineptr = (char *)malloc(*n);
        if (!*lineptr) return -1;
    }
    size_t len = 0;
    int c = EOF;
    while ((c = fgetc(stream)) != EOF) {
        if (len + 2 > *n) {
            size_t newn = *n * 2;
            char *newp = (char *)realloc(*lineptr, newn);
            if (!newp) return -1;
            *lineptr = newp;
            *n = newn;
        }
        (*lineptr)[len++] = (char)c;
        if (c == '\n') break;
    }
    if (len == 0 && c == EOF) return -1;
    (*lineptr)[len] = '\0';
    return (ssize_t)len;
}
#define getline wisp_getline

/* ---- S_ISREG / S_ISDIR ---- */
#ifndef S_ISREG
  #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
  #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

/* ---- signals MSVC doesn't define ---- */
#ifndef SIGHUP
  #define SIGHUP  1
#endif
#ifndef SIGQUIT
  #define SIGQUIT 3
#endif

/* ---- CTRL_WINDOW_BUFFER_SIZE_EVENT ---- */
#ifndef CTRL_WINDOW_BUFFER_SIZE_EVENT
  #define CTRL_WINDOW_BUFFER_SIZE_EVENT 0x0004
#endif

/* ---- console input mode flags ---- */
#ifndef ENABLE_VIRTUAL_TERMINAL_INPUT
  #define ENABLE_VIRTUAL_TERMINAL_INPUT 0x0200
#endif
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
  #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
  #define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif

#endif /* _WIN32 */
#endif /* WISP_POSIX_H */