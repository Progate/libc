#ifndef __wasilibc___typedef_sigset_t_h
#define __wasilibc___typedef_sigset_t_h

#include <features.h>

#ifdef __wasilibc_browseros
/* BrowserOS はシグナルを 1〜64 まで持つ（sigprocmask・sigaction の sa_mask がこの集合を使う）。形は musl と同じ */
typedef struct __sigset_t { unsigned long __bits[128/sizeof(long)]; } sigset_t;
#else
/* TODO: This is just a placeholder for now. Keep this in sync with musl. */
typedef unsigned char sigset_t;
#endif

#endif
