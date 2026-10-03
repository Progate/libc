// waitpid(2) / wait(2) / wait3 / wait4。子が終わるのを待つのはカーネルで、このプロセスはそのあいだ止まる
// （→ browser-os の src/process/wasi-spawn.ts の wait）。
//
// この OS にプロセスグループは無いので、`pid` が 0 や -pgid でも「起こした子のどれでも」（-1）として待つ。
// 止まった子・再開した子は居ない（この OS は止める動作のシグナルを捨てる。→ signal.c）ので報告しない。

#define _GNU_SOURCE
#include <browseros/host.h>
#include <errno.h>
#include <string.h>
#include <sys/resource.h>
#include <signal.h>
#include <sys/wait.h>

pid_t waitpid(pid_t pid, int *status, int options) {
  int32_t raw = 0, child = 0;
  int32_t error = __browseros_wait(pid > 0 ? pid : -1, options & WNOHANG, &raw, &child);
  if (error != 0) {
    errno = error;
    return -1;
  }
  if (status && child != 0)
    *status = raw;
  return child;
}

pid_t wait(int *status) { return waitpid(-1, status, 0); }

/// カーネルの wait の options に渡す WNOWAIT（報告するが回収しない）
#define BROWSEROS_WNOWAIT 0x1000000

int waitid(idtype_t type, id_t id, siginfo_t *info, int options) {
  if (!(options & (WEXITED | WSTOPPED | WCONTINUED)) || (type != P_ALL && type != P_PID && type != P_PGID)) {
    errno = EINVAL;
    return -1;
  }
  memset(info, 0, sizeof *info);
  // 止まった子・再開した子は居ないので、それだけを待つなら終わった子を見ない（WNOHANG なら 0 で戻る）
  if (!(options & WEXITED)) {
    if (options & WNOHANG)
      return 0;
    errno = ECHILD;
    return -1;
  }
  int32_t raw = 0, child = 0;
  int32_t kernel_options = (options & WNOHANG) | (options & WNOWAIT ? BROWSEROS_WNOWAIT : 0);
  int32_t error = __browseros_wait(type == P_PID ? (int32_t)id : -1, kernel_options, &raw, &child);
  if (error != 0) {
    errno = error;
    return -1;
  }
  if (child == 0)
    return 0;
  info->si_signo = SIGCHLD;
  info->si_pid = child;
  if (WIFSIGNALED(raw)) {
    info->si_code = CLD_KILLED;
    info->si_status = WTERMSIG(raw);
  } else {
    info->si_code = CLD_EXITED;
    info->si_status = WEXITSTATUS(raw);
  }
  return 0;
}

/// CPU 時間などの使用量は測れない（ブラウザーは他のスレッドの CPU 時間を教えない）ので 0 で返す
pid_t wait4(pid_t pid, int *status, int options, struct rusage *usage) {
  pid_t child = waitpid(pid, status, options);
  if (child > 0 && usage)
    memset(usage, 0, sizeof *usage);
  return child;
}

pid_t wait3(int *status, int options, struct rusage *usage) {
  return wait4(-1, status, options, usage);
}
