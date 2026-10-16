#ifndef __wasilibc___typedef_clockid_t_h
#define __wasilibc___typedef_clockid_t_h

#include <features.h>
#ifdef __wasilibc_browseros
/* BrowserOS: clockid_t は Linux と同じく時計の番号（CLOCK_REALTIME 0・CLOCK_MONOTONIC 1 …） */
typedef int clockid_t;
#else
typedef const struct __clockid *clockid_t;
#endif

#endif
