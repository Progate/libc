// インターバルタイマー（setitimer(2)・getitimer(2)）と、その上の alarm(2)・ualarm(3)・pause(2)。
//
// タイマーの持ち主はカーネルである（browser_os_process.setitimer / getitimer）。期限が来るとカーネルがこのプロセスへ
// SIGALRM を送るので、poll・パイプの read・nanosleep で止まっていても、ハンドラを置いていればその syscall が EINTR で
// 起きて戻るところでハンドラが走り、置いていなければ既定の動作で終わる（→ signal.c）。Linux と同じく、タイマーは
// プロセスに 1 つで、execve をまたいで残り、posix_spawn・fork の子には継がれない。
//
// 持つのは ITIMER_REAL（実時間）だけである。ITIMER_VIRTUAL・ITIMER_PROF（CPU 時間で数え、SIGVTALRM・SIGPROF を送る）は、
// wasm を syscall の境目でしか止められず、使った CPU 時間がその値に達したところで送れないので、カーネルが EINVAL で断る。

#include <browseros/host.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

/// カーネルへ渡せるマイクロ秒の上限（2^53 - 1。カーネルは JavaScript の数で持つ）
#define MAX_MICROSECONDS ((uint64_t)9007199254740991ULL)

/// timeval をマイクロ秒にする。範囲の外（負・tv_usec が 1 秒以上）なら -1。大きすぎる値は上限に丸める
static int to_microseconds(const struct timeval *value, uint64_t *out) {
  if (value->tv_sec < 0 || value->tv_usec < 0 || value->tv_usec >= 1000000)
    return -1;
  if ((uint64_t)value->tv_sec > (MAX_MICROSECONDS - (uint64_t)value->tv_usec) / 1000000)
    *out = MAX_MICROSECONDS;
  else
    *out = (uint64_t)value->tv_sec * 1000000 + (uint64_t)value->tv_usec;
  return 0;
}

static struct timeval to_timeval(uint64_t microseconds) {
  struct timeval value = {.tv_sec = (time_t)(microseconds / 1000000),
                          .tv_usec = (suseconds_t)(microseconds % 1000000)};
  return value;
}

static void to_itimerval(const uint64_t values[2], struct itimerval *out) {
  out->it_value = to_timeval(values[0]);
  out->it_interval = to_timeval(values[1]);
}

int getitimer(int which, struct itimerval *value) {
  uint64_t current[2];
  int32_t error = __browseros_getitimer(which, current);
  if (error != 0) {
    errno = error;
    return -1;
  }
  to_itimerval(current, value);
  return 0;
}

int setitimer(int which, const struct itimerval *restrict value, struct itimerval *restrict old) {
  // NULL は止めるのと同じに扱う（Linux と同じ）
  uint64_t next = 0, interval = 0;
  if (value && (to_microseconds(&value->it_value, &next) != 0 ||
                to_microseconds(&value->it_interval, &interval) != 0)) {
    errno = EINVAL;
    return -1;
  }
  uint64_t previous[2] = {0, 0};
  int32_t error = __browseros_setitimer(which, next, interval, previous);
  if (error != 0) {
    errno = error;
    return -1;
  }
  if (old)
    to_itimerval(previous, old);
  return 0;
}

unsigned alarm(unsigned seconds) {
  struct itimerval it = {.it_value.tv_sec = seconds}, old = {0};
  setitimer(ITIMER_REAL, &it, &old);
  // 前のタイマーの残り（秒に切り上げる。1 秒未満でも残っていれば 0 にしない）
  return old.it_value.tv_sec + !!old.it_value.tv_usec;
}

unsigned ualarm(unsigned value, unsigned interval) {
  struct itimerval it = {.it_interval.tv_sec = interval / 1000000,
                         .it_interval.tv_usec = interval % 1000000,
                         .it_value.tv_sec = value / 1000000,
                         .it_value.tv_usec = value % 1000000},
                   old = {0};
  setitimer(ITIMER_REAL, &it, &old);
  return old.it_value.tv_sec * 1000000 + old.it_value.tv_usec;
}

int pause(void) {
  // いまのマスクのまま、ハンドラが走るまで止まる（終わらせるシグナルならそこで終わる）。いつも -1 / EINTR で戻る
  sigset_t mask;
  sigprocmask(SIG_BLOCK, 0, &mask);
  return sigsuspend(&mask);
}
