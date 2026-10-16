// インターバルタイマー（setitimer(2)・getitimer(2)）と、その上の alarm(2)・ualarm(3)・pause(2)。
//
// タイマーの持ち主はカーネルである（setitimer(2)・getitimer(2)）。期限が来るとカーネルがこのプロセスへ
// SIGALRM を送るので、poll・パイプの read・nanosleep で止まっていても、ハンドラを置いていればその syscall が EINTR で
// 起きて戻るところでハンドラが走り、置いていなければ既定の動作で終わる（→ signal.c）。Linux と同じく、タイマーは
// プロセスに 1 つで、execve をまたいで残り、posix_spawn・fork の子には継がれない。
//
// 持つのは ITIMER_REAL（実時間）だけである。ITIMER_VIRTUAL・ITIMER_PROF（CPU 時間で数え、SIGVTALRM・SIGPROF を送る）は、
// wasm を syscall の境目でしか止められず、使った CPU 時間がその値に達したところで送れないので、カーネルが EINVAL で断る。

#include <browseros/linux.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <sys/time.h>
#include <unistd.h>

/// カーネルの struct __kernel_old_itimerval（riscv32 の Linux。long の秒とマイクロ秒。間隔が先）
struct kernel_itimerval {
  int32_t interval_sec, interval_usec, value_sec, value_usec;
};

/// 秒を long に収める（収まらない値は Linux の 32 ビットと同じく上限に丸める）
static int32_t clamp_seconds(time_t seconds) { return seconds > INT32_MAX ? INT32_MAX : (int32_t)seconds; }

static void from_kernel(const struct kernel_itimerval *raw, struct itimerval *out) {
  out->it_interval.tv_sec = raw->interval_sec;
  out->it_interval.tv_usec = raw->interval_usec;
  out->it_value.tv_sec = raw->value_sec;
  out->it_value.tv_usec = raw->value_usec;
}

int getitimer(int which, struct itimerval *value) {
  struct kernel_itimerval raw;
  long r = __bos_syscall2(__BOS_SYS_getitimer, which, __bos_ptr(&raw));
  if (r < 0)
    return __browseros_syscall_ret(r);
  from_kernel(&raw, value);
  return 0;
}

int setitimer(int which, const struct itimerval *restrict value, struct itimerval *restrict old) {
  // NULL は止めるのと同じに扱う（Linux と同じ）
  struct kernel_itimerval next = {0}, previous = {0};
  if (value) {
    next.interval_sec = clamp_seconds(value->it_interval.tv_sec);
    next.interval_usec = value->it_interval.tv_usec;
    next.value_sec = clamp_seconds(value->it_value.tv_sec);
    next.value_usec = value->it_value.tv_usec;
  }
  long r = __bos_syscall3(__BOS_SYS_setitimer, which, __bos_ptr(&next), __bos_ptr(&previous));
  if (r < 0)
    return __browseros_syscall_ret(r);
  if (old)
    from_kernel(&previous, old);
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
