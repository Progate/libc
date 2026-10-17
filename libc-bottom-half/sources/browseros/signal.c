// シグナルの動作（sigaction）・マスク（sigprocmask）・保留・待ち（sigsuspend・sigtimedwait）と、raise(3)。
//
// シグナルの状態はカーネルが持つ（Linux と同じ。→ browser-os の src/wasi/linux/signals.ts）。動作の表はプロセスに
// 1 つ、マスクはスレッドごとで、どれも rt_sigaction(2)・rt_sigprocmask(2) などで読み書きする。libc は写しを持たない。
//
// 配るところだけは libc が受け持つ。Linux のカーネルは syscall から戻るところでハンドラのフレームを積んで飛ばすが、
// wasm の積み重なりはカーネルから触れない。そこで、syscall から戻るところでランタイムが受け口 __browseros_signal
// を呼び（→ browser-wasi の signals.ts）、受け口はカーネルの browseros_sigdeliver で配るものとマスクを受け取って
// ハンドラを呼び、rt_sigreturn(2) でマスクを戻す。既定の動作（終わる・止まる）と無視はカーネルが済ませる。
//
// 関数ポインタは wasm では関数の表の番号である。表の 1 番は普通の関数でありうるので、Linux の SIG_IGN（1）と
// 重ならないよう、カーネルへはハンドラの番号に最上位のビットを立てて渡す（0 が SIG_DFL、1 が SIG_IGN）。

#define _GNU_SOURCE
#include <browseros/libc.h>
#include <browseros/linux.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/// カーネルの struct sigaction（riscv32 の Linux。SA_RESTORER は無い）
struct kernel_sigaction {
  uint32_t handler;
  uint32_t flags;
  uint64_t mask;
};

/// browseros_sigdeliver が書くフレーム（→ browser-os の src/wasi/linux/signals.ts）。`status` は大きさを渡したときだけ書かれる
struct signal_frame {
  int32_t sig;
  uint32_t handler;
  int32_t flags;
  int32_t code;
  int32_t pid;
  uint32_t uid;
  uint64_t oldmask;
  int32_t status;
  int32_t pad;
};

/// カーネルの siginfo_t（riscv32 の Linux。→ wait.c）
struct kernel_siginfo {
  int32_t signo, error, code;
  int32_t pid;
  uint32_t uid;
  int32_t status;
  int32_t pad[26];
};

/// カーネルの struct timespec（64 ビット）
struct kernel_timespec {
  int64_t tv_sec;
  int64_t tv_nsec;
};

#define KERNEL_SIG_DFL 0u
#define KERNEL_SIG_IGN 1u
#define KERNEL_HANDLER_BIT 0x80000000u

/// このスレッドでハンドラを走らせた回数（→ __browseros_handled_count）
static _Thread_local unsigned handled_count;

unsigned __browseros_handled_count(void) { return handled_count; }

void __SIG_IGN(int sig) { (void)sig; }

_Noreturn void __SIG_ERR(int sig) {
  (void)sig;
  __builtin_trap();
}

/// sigset_t の先頭 8 バイト（シグナル n はビット n-1。musl の sigset_t と同じ並び）
static uint64_t mask_of(const sigset_t *set) {
  uint64_t mask;
  memcpy(&mask, set, sizeof mask);
  return mask;
}

static void set_of(uint64_t mask, sigset_t *set) {
  memset(set, 0, sizeof *set);
  memcpy(set, &mask, sizeof mask);
}

static uint32_t encode_handler(void (*handler)(int)) {
  if (handler == SIG_DFL)
    return KERNEL_SIG_DFL;
  if (handler == SIG_IGN)
    return KERNEL_SIG_IGN;
  return (uint32_t)(uintptr_t)handler | KERNEL_HANDLER_BIT;
}

static void (*decode_handler(uint32_t handler))(int) {
  if (handler == KERNEL_SIG_DFL)
    return SIG_DFL;
  if (handler == KERNEL_SIG_IGN)
    return SIG_IGN;
  return (void (*)(int))(uintptr_t)(handler & ~KERNEL_HANDLER_BIT);
}

/// syscall から戻るところでランタイムが呼ぶ受け口。`hint` は共有の語から取り出したシグナル（0 なら無し）。
/// 戻り値は、EINTR で終わった syscall をやり直してよいか（走らせたハンドラがすべて SA_RESTART を持っていたか）
__attribute__((export_name("__browseros_signal"))) int __browseros_signal(int hint) {
  int saved = errno;
  int restart = 1;
  struct signal_frame frame;
  while (__bos_syscall3(__BOS_SYS_browseros_sigdeliver, hint, __bos_ptr(&frame), sizeof frame) > 0) {
    hint = 0;
    handled_count++;
    if (frame.flags & SA_SIGINFO) {
      siginfo_t info;
      memset(&info, 0, sizeof info);
      info.si_signo = frame.sig;
      info.si_code = frame.code;
      info.si_pid = frame.pid;
      info.si_uid = frame.uid;
      info.si_status = frame.status;
      ((void (*)(int, siginfo_t *, void *))(uintptr_t)(frame.handler & ~KERNEL_HANDLER_BIT))(frame.sig, &info, 0);
    } else {
      decode_handler(frame.handler)(frame.sig);
    }
    if (!(frame.flags & SA_RESTART))
      restart = 0;
    __bos_syscall1(__BOS_SYS_rt_sigreturn, __bos_ptr(&frame));
  }
  errno = saved;
  return restart;
}

/// 変えられない（SIGKILL・SIGSTOP）か、musl が内部用に取っておく番号（32〜34）か
static int reserved(int sig) { return sig == SIGKILL || sig == SIGSTOP || (unsigned)sig - 32U < 3; }

int raise(int sig) {
  // musl と同じく、送るあいだはアプリのシグナルを塞ぎ、戻してから配らせる（tkill(2) は自分のスレッドへ送る）
  sigset_t all, saved;
  sigfillset(&all);
  pthread_sigmask(SIG_BLOCK, &all, &saved);
  long r = __bos_syscall2(__BOS_SYS_tkill, gettid(), sig);
  pthread_sigmask(SIG_SETMASK, &saved, 0);
  return __browseros_syscall_ret(r);
}

pid_t gettid(void) {
  long tid = __bos_syscall0(__BOS_SYS_gettid);
  return tid < 0 ? getpid() : tid;
}

void __browseros_broken_pipe(void) {
  int saved = errno;
  raise(SIGPIPE);
  errno = saved;
}

int sigaction(int sig, const struct sigaction *restrict act, struct sigaction *restrict old) {
  if (sig < 1 || sig >= _NSIG || (act && reserved(sig))) {
    errno = EINVAL;
    return -1;
  }
  struct kernel_sigaction next, previous;
  if (act) {
    next.handler = act->sa_flags & SA_SIGINFO ? (uint32_t)(uintptr_t)act->sa_sigaction | KERNEL_HANDLER_BIT
                                              : encode_handler(act->sa_handler);
    next.flags = act->sa_flags;
    next.mask = mask_of(&act->sa_mask);
  }
  long r = __bos_syscall4(__BOS_SYS_rt_sigaction, sig, act ? __bos_ptr(&next) : 0, old ? __bos_ptr(&previous) : 0, 8);
  if (r < 0)
    return __browseros_syscall_ret(r);
  if (old) {
    memset(old, 0, sizeof *old);
    if (previous.flags & SA_SIGINFO)
      old->sa_sigaction = (void (*)(int, siginfo_t *, void *))(uintptr_t)(previous.handler & ~KERNEL_HANDLER_BIT);
    else
      old->sa_handler = decode_handler(previous.handler);
    old->sa_flags = previous.flags;
    set_of(previous.mask, &old->sa_mask);
  }
  return 0;
}

void (*signal(int sig, void (*func)(int)))(int) {
  struct sigaction act = {.sa_handler = func, .sa_flags = SA_RESTART}, old;
  if (sigaction(sig, &act, &old) < 0)
    return SIG_ERR;
  return old.sa_handler;
}

extern __typeof(signal) bsd_signal __attribute__((__weak__, __alias__("signal")));
extern __typeof(signal) __sysv_signal __attribute__((__weak__, __alias__("signal")));

int pthread_sigmask(int how, const sigset_t *restrict set, sigset_t *restrict old) {
  uint64_t next = set ? mask_of(set) : 0, previous = 0;
  if (set && how != SIG_BLOCK && how != SIG_UNBLOCK && how != SIG_SETMASK)
    return EINVAL;
  long r = __bos_syscall4(__BOS_SYS_rt_sigprocmask, how, set ? __bos_ptr(&next) : 0, old ? __bos_ptr(&previous) : 0, 8);
  if (r < 0)
    return -r;
  if (old)
    set_of(previous, old);
  return 0;
}

int sigprocmask(int how, const sigset_t *restrict set, sigset_t *restrict old) {
  int error = pthread_sigmask(how, set, old);
  if (error) {
    errno = error;
    return -1;
  }
  return 0;
}

int sigpending(sigset_t *set) {
  uint64_t pending = 0;
  long r = __bos_syscall2(__BOS_SYS_rt_sigpending, __bos_ptr(&pending), 8);
  if (r < 0)
    return __browseros_syscall_ret(r);
  set_of(pending, set);
  return 0;
}

int sigsuspend(const sigset_t *mask) {
  uint64_t value = mask_of(mask);
  return __browseros_syscall_ret(__bos_syscall2(__BOS_SYS_rt_sigsuspend, __bos_ptr(&value), 8));
}

int sigtimedwait(const sigset_t *restrict set, siginfo_t *restrict info, const struct timespec *restrict timeout) {
  uint64_t wanted = mask_of(set);
  struct kernel_siginfo raw;
  struct kernel_timespec limit;
  if (timeout) {
    limit.tv_sec = timeout->tv_sec;
    limit.tv_nsec = timeout->tv_nsec;
  }
  long r = __bos_syscall4(__BOS_SYS_rt_sigtimedwait_time64, __bos_ptr(&wanted), __bos_ptr(&raw),
                          timeout ? __bos_ptr(&limit) : 0, 8);
  if (r < 0)
    return __browseros_syscall_ret(r);
  if (info) {
    memset(info, 0, sizeof *info);
    info->si_signo = raw.signo;
    info->si_code = raw.code;
    info->si_pid = raw.pid;
    info->si_uid = raw.uid;
    info->si_status = raw.status;
  }
  return r;
}

int sigwaitinfo(const sigset_t *restrict set, siginfo_t *restrict info) { return sigtimedwait(set, info, 0); }

int sigwait(const sigset_t *restrict set, int *restrict sig) {
  int taken = sigwaitinfo(set, 0);
  if (taken < 0)
    return errno;
  *sig = taken;
  return 0;
}
