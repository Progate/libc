#ifndef __wasilibc___struct_rusage_h
#define __wasilibc___struct_rusage_h

#include <__struct_timeval.h>

#ifdef __wasilibc_browseros
#include <features.h>

/*
 * BrowserOS: Linux（musl）と同じメンバーを持つ。wait3 / wait4 / getrusage の結果をそのまま読むプログラム
 * （CPython の os.wait4・resource.getrusage）は ru_maxrss などを名前で読むので、無いとコンパイルが通らない。
 * BrowserOS が数えているのは CPU 時間だけなので、残りは 0 で埋まる（→ sources/browseros/clocks.c）
 */
struct rusage {
  struct timeval ru_utime;
  struct timeval ru_stime;
  long ru_maxrss;
  long ru_ixrss;
  long ru_idrss;
  long ru_isrss;
  long ru_minflt;
  long ru_majflt;
  long ru_nswap;
  long ru_inblock;
  long ru_oublock;
  long ru_msgsnd;
  long ru_msgrcv;
  long ru_nsignals;
  long ru_nvcsw;
  long ru_nivcsw;
  long __reserved[16];
};
#else
/* TODO: Add more features here. */
struct rusage {
  struct timeval ru_utime;
  struct timeval ru_stime;
};
#endif

#endif
