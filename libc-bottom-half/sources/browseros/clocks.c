// プロセスの CPU 時間: CLOCK_PROCESS_CPUTIME_ID / CLOCK_THREAD_CPUTIME_ID・clock(3)・getrusage(2)・times(2)。
//
// CPU 時間はカーネルの時計（clock_gettime(2) の CLOCK_PROCESS_CPUTIME_ID・CLOCK_THREAD_CPUTIME_ID）が答える。
// BrowserOS はワーカーが始まってからの時間から syscall やパイプや眠りを待って止まっていた時間を引いて返す
// （→ browser-wasi の src/clock.ts）。wasi-libc の wasi-emulated-process-clocks は経過時間をそのまま返すので、
// 待っているだけのプロセスも CPU を使い続けたように見える。
//
// 単位は Linux と同じ: clock(3) は CLOCKS_PER_SEC（1000000）で、times(2) は CLK_TCK（100。sysconf(_SC_CLK_TCK)）。
//
// 子の CPU 時間（RUSAGE_CHILDREN・tms_cutime）は 0 で返す。カーネルが子の CPU 時間を親へ渡していない。

#include <errno.h>
#include <stdint.h>
#include <sys/resource.h>
#include <sys/times.h>
#include <time.h>

/// times(2) の刻み（Linux の USER_HZ）
#define CLK_TCK_HZ 100

/// 時計の値（ナノ秒）。読めなければ -1
static int64_t clock_ns(clockid_t clock) {
  struct timespec now;
  if (clock_gettime(clock, &now) != 0)
    return -1;
  return (int64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}

// アプリケーションが自分の clock を定義できるよう、libc の名前は __clock にして clock は弱い別名にする
clock_t __clock(void) {
  int64_t ns = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
  return ns < 0 ? (clock_t)-1 : (clock_t)(ns / (1000000000 / CLOCKS_PER_SEC));
}
__attribute__((__weak__, __alias__("__clock"))) clock_t clock(void);

static struct timeval timeval_of(int64_t ns) {
  if (ns < 0)
    ns = 0;
  return (struct timeval){.tv_sec = ns / 1000000000, .tv_usec = (ns % 1000000000) / 1000};
}

int getrusage(int who, struct rusage *usage) {
  switch (who) {
  case RUSAGE_SELF:
    *usage = (struct rusage){.ru_utime = timeval_of(clock_ns(CLOCK_PROCESS_CPUTIME_ID))};
    return 0;
  case RUSAGE_THREAD:
    *usage = (struct rusage){.ru_utime = timeval_of(clock_ns(CLOCK_THREAD_CPUTIME_ID))};
    return 0;
  case RUSAGE_CHILDREN:
    *usage = (struct rusage){0};
    return 0;
  default:
    errno = EINVAL;
    return -1;
  }
}

clock_t times(struct tms *buffer) {
  int64_t cpu = clock_ns(CLOCK_PROCESS_CPUTIME_ID);
  if (buffer)
    *buffer = (struct tms){.tms_utime = (clock_t)((cpu < 0 ? 0 : cpu) / (1000000000 / CLK_TCK_HZ))};
  int64_t now = clock_ns(CLOCK_MONOTONIC);
  return (clock_t)(now / (1000000000 / CLK_TCK_HZ));
}
