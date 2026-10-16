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

#include <features.h>
#ifdef __wasilibc_browseros
/*
 * BrowserOS: 時計は Linux（musl の include/time.h）と同じ番号で、カーネルの clock_gettime(2) へそのまま渡す。
 * CLOCKS_PER_SEC も Linux と同じく 1000000（XSI の決まり）
 */
#define CLOCK_REALTIME           0
#define CLOCK_MONOTONIC          1
#define CLOCK_PROCESS_CPUTIME_ID 2
#define CLOCK_THREAD_CPUTIME_ID  3
#define CLOCK_MONOTONIC_RAW      4
#define CLOCK_REALTIME_COARSE    5
#define CLOCK_MONOTONIC_COARSE   6
#define CLOCK_BOOTTIME           7
#define CLOCK_REALTIME_ALARM     8
#define CLOCK_BOOTTIME_ALARM     9
#define CLOCK_SGI_CYCLE         10
#define CLOCK_TAI               11

#define TIME_UTC 1

#define CLOCKS_PER_SEC 1000000L
#else
extern const struct __clockid _CLOCK_MONOTONIC;
#define CLOCK_MONOTONIC (&_CLOCK_MONOTONIC)
extern const struct __clockid _CLOCK_REALTIME;
#define CLOCK_REALTIME (&_CLOCK_REALTIME)

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

#endif
