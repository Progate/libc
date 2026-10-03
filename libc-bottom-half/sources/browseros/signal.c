// シグナルの動作（sigaction）・マスク（sigprocmask）・保留と、raise(3)。
//
// 動作の表はプロセスに 1 つ、マスクと保留はスレッドごとに持つ（POSIX と同じ。raise は呼んだスレッドへ届く）。
// ハンドラの無いシグナルの既定の動作は Linux と同じで、終わるもの（SIGPIPE・SIGTERM・SIGABRT など）は
// カーネルの exit_signal で終わる。親の waitpid からは kill(2) で止められたのと同じく WIFSIGNALED に見える。
//
// 止める動作（SIGSTOP・SIGTSTP・SIGTTIN・SIGTTOU）は捨てる。この OS にジョブ制御は無く、どのプロセスも
// 制御端末を持つセッションに属さないので、POSIX が「孤立したプロセスグループでは捨てる」と定める場合に当たる。
//
// 届くのはこのプロセスの中で起きたシグナル（raise・abort・kill(getpid())・読む端の無いパイプへの write の
// SIGPIPE）である。他のプロセスからの kill(2) はカーネルが既定の動作で止める（→ browser-os の process/kernel.ts）。

#define _GNU_SOURCE
#include <browseros/host.h>
#include <browseros/libc.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/// 各シグナルの動作。0 で埋まった `sa_handler`（SIG_DFL）が初期状態
static struct sigaction actions[_NSIG];

/// このスレッドが塞いでいるシグナル
static _Thread_local sigset_t blocked;

/// 塞がれているあいだに届き、まだ配っていないシグナル
static _Thread_local sigset_t pending;

void __SIG_IGN(int sig) { (void)sig; }

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

int sigsuspend(const sigset_t *mask) {
  sigset_t saved = blocked;
  blocked = *mask;
  sigdelset(&blocked, SIGKILL);
  sigdelset(&blocked, SIGSTOP);
  for (int sig = 1; sig < _NSIG; sig++) {
    if (sigismember(&pending, sig) && !sigismember(&blocked, sig)) {
      sigdelset(&pending, sig);
      deliver(sig);
      blocked = saved;
      deliver_pending();
      errno = EINTR;
      return -1;
    }
  }
  /*
   * 届くシグナルはこのスレッドの中で起きるものだけなので、保留が無ければもう何も届かない。
   * 他のプロセスからの kill(2) はカーネルがこのプロセスを止めるので、それまで眠る（本物の sigsuspend と同じく戻らない）
   */
  for (;;)
    sleep(3600);
}

int sigtimedwait(const sigset_t *restrict set, siginfo_t *restrict info,
                 const struct timespec *restrict timeout) {
  int sig = take_pending(set);
  if (sig == 0) {
    // 保留が無ければ、待っても届かない（sigsuspend と同じ理由）。時間切れまで眠って EAGAIN
    if (!timeout) {
      for (;;)
        sleep(3600);
    }
    nanosleep(timeout, 0);
    errno = EAGAIN;
    return -1;
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
