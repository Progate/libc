// プロセスの CPU 時間: CLOCK_PROCESS_CPUTIME_ID / CLOCK_THREAD_CPUTIME_ID・clock(3)・getrusage(2)・times(2)。
//
// CPU 時間はカーネルの時計（WASI の clockid 2 / 3）が答える。BrowserOS はワーカーが始まってからの時間から
// syscall やパイプや眠りを待って止まっていた時間を引いて返す（→ browser-wasi の src/clock.ts）。
// wasi-libc の wasi-emulated-process-clocks は経過時間をそのまま返すので、待っているだけのプロセスも
// CPU を使い続けたように見える。
//
// 子の CPU 時間（RUSAGE_CHILDREN・tms_cutime）は 0 で返す。カーネルが子の CPU 時間を親へ渡していない。

#include <common/clock.h>
#include <common/time.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <time.h>
#include <wasi/api.h>

_Static_assert(CLOCKS_PER_SEC == NSEC_PER_SEC, "clock() はナノ秒で返す");

const struct __clockid _CLOCK_PROCESS_CPUTIME_ID = {.id = __WASI_CLOCKID_PROCESS_CPUTIME_ID};
const struct __clockid _CLOCK_THREAD_CPUTIME_ID = {.id = __WASI_CLOCKID_THREAD_CPUTIME_ID};

/// このプロセスの CPU 時間（ナノ秒）。読めなければ -1（clock(3) と同じ）
static clock_t cpu_time(void) {
  __wasi_timestamp_t ns;
  if (__wasi_clock_time_get(__WASI_CLOCKID_PROCESS_CPUTIME_ID, 1, &ns) != 0)
    return (clock_t)-1;
  return (clock_t)ns;
}

// アプリケーションが自分の clock を定義できるよう、libc の名前は __clock にして clock は弱い別名にする
clock_t __clock(void) { return cpu_time(); }
__attribute__((__weak__, __alias__("__clock"))) clock_t clock(void);

int getrusage(int who, struct rusage *usage) {
  switch (who) {
  case RUSAGE_SELF: {
    clock_t user = cpu_time();
    *usage = (struct rusage){.ru_utime = timestamp_to_timeval(user == (clock_t)-1 ? 0 : user)};
    return 0;
  }
  case RUSAGE_CHILDREN:
    *usage = (struct rusage){0};
    return 0;
  default:
    errno = EINVAL;
    return -1;
  }
}

clock_t times(struct tms *buffer) {
  *buffer = (struct tms){.tms_utime = cpu_time()};
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return now.tv_sec * NSEC_PER_SEC + now.tv_nsec;
}
