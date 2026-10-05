// シグナルの動作（sigaction）・マスク（sigprocmask）・保留と、raise(3)。
//
// 動作の表はプロセスに 1 つ、マスクと保留はスレッドごとに持つ（POSIX と同じ。raise は呼んだスレッドへ届く）。
// ハンドラの無いシグナルの既定の動作は Linux と同じで、終わるもの（SIGPIPE・SIGTERM・SIGABRT など）は
// カーネルの exit_signal で終わる。親の waitpid からは kill(2) で止められたのと同じく WIFSIGNALED に見える。
//
// 止める動作（SIGSTOP・SIGTSTP・SIGTTIN・SIGTTOU）は捨てる。この OS にジョブ制御は無く、どのプロセスも
// 制御端末を持つセッションに属さないので、POSIX が「孤立したプロセスグループでは捨てる」と定める場合に当たる。
//
// 始めるときの扱いはカーネルが決めている（posix_spawn の子は、親が無視していたものを無視し、塞いでいたものを
// 塞いだまま始まる）。起動時に読んで、動作の表とマスクの初期値にする。親の扱いは、posix_spawn の前に
// __browseros_report_signals でカーネルへ知らせる（本物ではカーネルがもともと持っているもの）。
//
// 届くのは、このプロセスの中で起きたシグナル（raise・abort・kill(getpid())・読む端の無いパイプへの write の
// SIGPIPE）と、カーネルが送るシグナル（子が終わったときの SIGCHLD・他のプロセスからの kill(2)・端末の Ctrl+C）である。
// カーネルが送るものは、ハンドラを置いているときだけ保留に積まれる（置いていなければカーネルが既定の動作にする）。
// ハンドラを置いたら __browseros_set_handlers で知らせる。カーネルは止まっている syscall を EINTR で起こし、
// syscall から戻るところでランタイムが __browseros_signal を呼ぶ（Unix のカーネルがユーザー空間へ戻るところで
// ハンドラを走らせるのと同じ。→ browser-wasi の signals.ts）。

#define _GNU_SOURCE
#include <browseros/host.h>
#include <browseros/libc.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <wasi/api.h>

/// 各シグナルの動作。0 で埋まった `sa_handler`（SIG_DFL）が初期状態
static struct sigaction actions[_NSIG];

/// このスレッドが塞いでいるシグナル
static _Thread_local sigset_t blocked;

/// 塞がれているあいだに届き、まだ配っていないシグナル
static _Thread_local sigset_t pending;

/// このスレッドでハンドラを走らせた回数（→ __browseros_handled_count）
static _Thread_local unsigned handled_count;

unsigned __browseros_handled_count(void) { return handled_count; }

void __SIG_IGN(int sig) { (void)sig; }

/// u64（シグナル n はビット n-1）と sigset_t の先頭を行き来する。sigset_t の並びは musl と同じ（→ sigaddset.c）
static uint64_t mask_of(const sigset_t *set) {
  uint64_t mask = 0;
  for (int sig = 1; sig < _NSIG; sig++)
    if (sigismember(set, sig))
      mask |= (uint64_t)1 << (sig - 1);
  return mask;
}

static void set_of(uint64_t mask, sigset_t *set) {
  sigemptyset(set);
  for (int sig = 1; sig < _NSIG; sig++)
    if (mask & ((uint64_t)1 << (sig - 1)))
      sigaddset(set, sig);
}

// main より前（stdio が書き始めるより前）に読む。読めないホストでは既定の動作で、塞がずに始める
static void report_handlers(void);

__attribute__((constructor(10))) static void adopt_initial_signals(void) {
  uint64_t ignored = 0, mask = 0;
  if (__browseros_initial_signals(&ignored, &mask) != 0)
    return;
  for (int sig = 1; sig < _NSIG; sig++)
    if (ignored & ((uint64_t)1 << (sig - 1)))
      actions[sig].sa_handler = SIG_IGN;
  set_of(mask, &blocked);
  report_handlers();
}

/// 送られたときに保留へ積んでほしいシグナルをカーネルへ知らせる。ハンドラを置いたものと、塞いでいるもの
/// （塞いでいるあいだは保留に残り、外したときに配られる。sigwait で待つのもこれ）。それ以外はカーネルが既定の動作にする
static void report_handlers(void) {
  uint64_t caught = mask_of(&blocked);
  for (int sig = 1; sig < _NSIG; sig++)
    if (actions[sig].sa_handler != SIG_DFL && actions[sig].sa_handler != SIG_IGN)
      caught |= (uint64_t)1 << (sig - 1);
  __browseros_set_handlers(caught);
}

void __browseros_report_signals(void) {
  // 捕まえているシグナルは子では既定に戻る（ハンドラは子に無い）。無視しているものだけを知らせる
  sigset_t ignored;
  sigemptyset(&ignored);
  for (int sig = 1; sig < _NSIG; sig++)
    if (actions[sig].sa_handler == SIG_IGN)
      sigaddset(&ignored, sig);
  __browseros_set_signals(mask_of(&ignored), mask_of(&blocked));
}

_Noreturn void __SIG_ERR(int sig) {
  (void)sig;
  __builtin_trap();
}

/// 変えられない（SIGKILL・SIGSTOP）か、musl が内部用に取っておく番号（32〜34）か
static int reserved(int sig) { return sig == SIGKILL || sig == SIGSTOP || (unsigned)sig - 32U < 3; }

static int valid(int sig) { return sig > 0 && sig < _NSIG; }

/// 既定の動作が「無視」か「止める」（この OS では捨てる）か
static int discarded_by_default(int sig) {
  switch (sig) {
  case SIGCHLD:
  case SIGURG:
  case SIGWINCH:
  case SIGCONT:
  case SIGSTOP:
  case SIGTSTP:
  case SIGTTIN:
  case SIGTTOU:
    return 1;
  default:
    return 0;
  }
}

/// 既定の動作で終わる。カーネルの外（exit_signal を渡さないホスト）では、シェルと同じ `128 + 番号` で終わる
_Noreturn static void terminate(int sig) {
  __browseros_exit_signal(sig);
  _Exit(128 + sig);
}

static void deliver_pending(void);

/// 1 つ配る。塞がれていれば保留に積む
static void deliver(int sig) {
  if (sigismember(&blocked, sig)) {
    sigaddset(&pending, sig);
    return;
  }
  struct sigaction action = actions[sig];
  if (action.sa_handler == SIG_IGN)
    return;
  if (action.sa_handler == SIG_DFL) {
    if (discarded_by_default(sig))
      return;
    terminate(sig);
  }
  // ハンドラのあいだは sa_mask と（SA_NODEFER でなければ）そのシグナル自身を塞ぎ、戻ったら元のマスクに戻す
  sigset_t saved = blocked;
  sigorset(&blocked, &blocked, &action.sa_mask);
  if (!(action.sa_flags & SA_NODEFER))
    sigaddset(&blocked, sig);
  if (action.sa_flags & SA_RESETHAND) {
    actions[sig].sa_handler = SIG_DFL;
    actions[sig].sa_flags &= ~SA_SIGINFO;
  }
  handled_count++;
  if (action.sa_flags & SA_SIGINFO) {
    siginfo_t info;
    memset(&info, 0, sizeof info);
    info.si_signo = sig;
    info.si_code = SI_USER;
    info.si_pid = getpid();
    action.sa_sigaction(sig, &info, 0);
  } else {
    action.sa_handler(sig);
  }
  blocked = saved;
  deliver_pending();
}

/// 塞ぐのをやめたシグナルのうち、保留にあるものを番号の小さい順に配る
static void deliver_pending(void) {
  for (int sig = 1; sig < _NSIG; sig++) {
    if (sigismember(&pending, sig) && !sigismember(&blocked, sig)) {
      sigdelset(&pending, sig);
      deliver(sig);
    }
  }
}

/// カーネルが送ったシグナルを、syscall から戻るところで配る口（ランタイムが呼ぶ。→ browser-wasi の signals.ts）。
/// 塞いでいればこのスレッドの保留に入る。戻り値は、その syscall を EINTR で終える代わりにやり直してよいか
/// （ハンドラが SA_RESTART を持つか。ハンドラの無いシグナルは何も中断していないのでやり直してよい）
__attribute__((export_name("__browseros_signal"))) int __browseros_signal(int sig) {
  if (!valid(sig))
    return 1;
  int saved = errno;
  struct sigaction action = actions[sig];
  deliver(sig);
  errno = saved;
  int caught = action.sa_handler != SIG_DFL && action.sa_handler != SIG_IGN;
  return !caught || (action.sa_flags & SA_RESTART) != 0;
}

int raise(int sig) {
  if (!valid(sig)) {
    errno = EINVAL;
    return -1;
  }
  deliver(sig);
  return 0;
}

void __browseros_broken_pipe(void) {
  int saved = errno;
  raise(SIGPIPE);
  errno = saved;
}

int sigaction(int sig, const struct sigaction *restrict act, struct sigaction *restrict old) {
  if (!valid(sig) || (act && reserved(sig))) {
    errno = EINVAL;
    return -1;
  }
  if (old)
    *old = actions[sig];
  if (act) {
    actions[sig] = *act;
    // 無視にしたシグナルの保留は捨てる（POSIX: 保留中のシグナルを SIG_IGN にすると捨てられる）
    if (act->sa_handler == SIG_IGN || (act->sa_handler == SIG_DFL && discarded_by_default(sig)))
      sigdelset(&pending, sig);
    report_handlers();
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
  if (old)
    *old = blocked;
  if (!set)
    return 0;
  sigset_t next;
  switch (how) {
  case SIG_BLOCK:
    sigorset(&next, &blocked, set);
    break;
  case SIG_UNBLOCK:
    next = blocked;
    for (int sig = 1; sig < _NSIG; sig++)
      if (sigismember(set, sig))
        sigdelset(&next, sig);
    break;
  case SIG_SETMASK:
    next = *set;
    break;
  default:
    return EINVAL;
  }
  // SIGKILL と SIGSTOP は塞げない（黙って外す。Linux と同じ）
  sigdelset(&next, SIGKILL);
  sigdelset(&next, SIGSTOP);
  blocked = next;
  report_handlers();
  deliver_pending();
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
  *set = pending;
  return 0;
}

/// 保留から `set` に入っている番号を 1 つ取り出す。無ければ 0
static int take_pending(const sigset_t *set) {
  for (int sig = 1; sig < _NSIG; sig++) {
    if (sigismember(&pending, sig) && sigismember(set, sig)) {
      sigdelset(&pending, sig);
      return sig;
    }
  }
  return 0;
}

/// シグナルが配られるまで止まる。カーネルは保留を積むと止まっている poll_oneoff を EINTR で起こし、ランタイムが
/// 戻るところでハンドラを走らせる。時間切れなら 0、起こされたら -1（EINTR）
static int wait_for_signal(const struct timespec *timeout) {
  __wasi_subscription_t subscription = {
      .u.tag = __WASI_EVENTTYPE_CLOCK,
      .u.u.clock.id = __WASI_CLOCKID_MONOTONIC,
      // 締め切りの無い待ちは、届かない限り戻らない長さにする（poll_oneoff は待つものが 1 つは要る）
      .u.u.clock.timeout = timeout ? (__wasi_timestamp_t)timeout->tv_sec * 1000000000 + timeout->tv_nsec
                                   : UINT64_MAX / 2,
  };
  __wasi_event_t event;
  size_t nevents;
  __wasi_errno_t error = __wasi_poll_oneoff(&subscription, &event, 1, &nevents);
  if (error == __WASI_ERRNO_INTR) {
    errno = EINTR;
    return -1;
  }
  return 0;
}

int sigsuspend(const sigset_t *mask) {
  sigset_t saved = blocked;
  unsigned before = handled_count;
  blocked = *mask;
  sigdelset(&blocked, SIGKILL);
  sigdelset(&blocked, SIGSTOP);
  // 保留にあったものは、マスクを外した時点で配る
  deliver_pending();
  // 何か配られるまで待つ（塞いだままのシグナルは保留に入るだけで、起きてもまた待つ）
  while (handled_count == before)
    wait_for_signal(0);
  blocked = saved;
  deliver_pending();
  errno = EINTR;
  return -1;
}

int sigtimedwait(const sigset_t *restrict set, siginfo_t *restrict info,
                 const struct timespec *restrict timeout) {
  int sig = take_pending(set);
  if (sig == 0) {
    // 待っているシグナルは塞がれているので、届けば保留に入る。届くか時間切れまで待つ
    struct timespec deadline;
    if (timeout) {
      clock_gettime(CLOCK_MONOTONIC, &deadline);
      deadline.tv_sec += timeout->tv_sec;
      deadline.tv_nsec += timeout->tv_nsec;
      if (deadline.tv_nsec >= 1000000000) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000;
      }
    }
    while ((sig = take_pending(set)) == 0) {
      struct timespec rest;
      if (timeout) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        rest.tv_sec = deadline.tv_sec - now.tv_sec;
        rest.tv_nsec = deadline.tv_nsec - now.tv_nsec;
        if (rest.tv_nsec < 0) {
          rest.tv_sec--;
          rest.tv_nsec += 1000000000;
        }
        if (rest.tv_sec < 0) {
          errno = EAGAIN;
          return -1;
        }
      }
      if (wait_for_signal(timeout ? &rest : 0) == 0 && timeout) {
        if ((sig = take_pending(set)) != 0)
          break;
        errno = EAGAIN;
        return -1;
      }
    }
  }
  if (info) {
    memset(info, 0, sizeof *info);
    info->si_signo = sig;
    info->si_code = SI_USER;
    info->si_pid = getpid();
  }
  return sig;
}

int sigwaitinfo(const sigset_t *restrict set, siginfo_t *restrict info) {
  return sigtimedwait(set, info, 0);
}

int sigwait(const sigset_t *restrict set, int *restrict sig) {
  int taken = sigwaitinfo(set, 0);
  if (taken < 0)
    return errno;
  *sig = taken;
  return 0;
}
