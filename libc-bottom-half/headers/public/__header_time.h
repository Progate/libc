#ifndef __wasilibc___header_time_h
#define __wasilibc___header_time_h

#define __need_size_t
#define __need_NULL
#include <stddef.h>

#include <__struct_timespec.h>
#include <__struct_tm.h>
#include <__typedef_clockid_t.h>
#include <__typedef_time_t.h>

#define TIMER_ABSTIME 1

extern const struct __clockid _CLOCK_MONOTONIC;
#define CLOCK_MONOTONIC (&_CLOCK_MONOTONIC)
extern const struct __clockid _CLOCK_REALTIME;
#define CLOCK_REALTIME (&_CLOCK_REALTIME)

#include <features.h>
#ifdef __wasilibc_browseros
/* BrowserOS のカーネルは CPU 時間の時計を持つ（→ libc-bottom-half/sources/browseros/clocks.c） */
extern const struct __clockid _CLOCK_PROCESS_CPUTIME_ID;
#define CLOCK_PROCESS_CPUTIME_ID (&_CLOCK_PROCESS_CPUTIME_ID)
extern const struct __clockid _CLOCK_THREAD_CPUTIME_ID;
#define CLOCK_THREAD_CPUTIME_ID (&_CLOCK_THREAD_CPUTIME_ID)
#endif

/*
 * TIME_UTC is the only standardized time base value.
 */
#define TIME_UTC 1

/*
 * Note that XSI specifies CLOCKS_PER_SEC to be 1000000, rather than
 * 1000000000; the clock API is providing more precision than XSI specifies.
 */
#define CLOCKS_PER_SEC ((clock_t)1000000000)

#endif
