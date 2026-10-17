#ifndef __wasilibc___header_sys_resource_h
#define __wasilibc___header_sys_resource_h

#include <__struct_rusage.h>

/* BrowserOS: getrusage(2) の who は Linux の値 */
#define RUSAGE_SELF 0
#define RUSAGE_CHILDREN (-1)
#define RUSAGE_THREAD 1

#ifdef __wasilibc_browseros
/* BrowserOS: 資源の上限（カーネルの prlimit64(2)。→ browseros/resource.c）。値は Linux のもの */
typedef unsigned long long rlim_t;

struct rlimit {
  rlim_t rlim_cur;
  rlim_t rlim_max;
};

#define RLIM_INFINITY (~0ULL)
#define RLIM_SAVED_CUR RLIM_INFINITY
#define RLIM_SAVED_MAX RLIM_INFINITY

#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_CORE 4
#define RLIMIT_RSS 5
#define RLIMIT_NPROC 6
#define RLIMIT_NOFILE 7
#define RLIMIT_MEMLOCK 8
#define RLIMIT_AS 9
#define RLIMIT_LOCKS 10
#define RLIMIT_SIGPENDING 11
#define RLIMIT_MSGQUEUE 12
#define RLIMIT_NICE 13
#define RLIMIT_RTPRIO 14
#define RLIMIT_RTTIME 15
#define RLIMIT_NLIMITS 16
#define RLIM_NLIMITS RLIMIT_NLIMITS

#if defined(_LARGEFILE64_SOURCE) || defined(_GNU_SOURCE)
#define RLIM64_INFINITY RLIM_INFINITY
#define RLIM64_SAVED_CUR RLIM_SAVED_CUR
#define RLIM64_SAVED_MAX RLIM_SAVED_MAX
#define getrlimit64 getrlimit
#define setrlimit64 setrlimit
#define rlimit64 rlimit
#define rlim64_t rlim_t
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

int getrusage(int who, struct rusage *usage);
#ifdef __wasilibc_browseros
int getrlimit(int, struct rlimit *);
int setrlimit(int, const struct rlimit *);
#ifdef _GNU_SOURCE
int prlimit(pid_t, int, const struct rlimit *, struct rlimit *);
#define prlimit64 prlimit
#endif
#endif

#ifdef __cplusplus
}
#endif

#endif
