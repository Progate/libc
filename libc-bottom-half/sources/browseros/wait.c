// waitpid(2) / wait(2) / wait3 / wait4 / waitid(2)。子が終わるのを待つのはカーネルの waitid（riscv32 の Linux には
// wait4 が無く、waitid だけがある。musl の emulate_wait4.c と同じく、waitpid を waitid で書く）。
//
// `pid` が 0 なら自分のグループの子、-pgid ならそのグループの子を待つ。止まった子・再開した子は居ない（この OS は
// 止める動作のシグナルを捨てる。→ signal.c）ので報告しない。

#define _GNU_SOURCE
#include <browseros/libc.h>
#include <browseros/linux.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/resource.h>
#include <signal.h>
#include <sys/wait.h>

/// waitid(2) の idtype と options（Linux の値。この libc のヘッダーと同じ）
#define BOS_P_ALL 0
#define BOS_P_PID 1
#define BOS_P_PGID 2

/// カーネルが書く siginfo_t（riscv32 の Linux の形。`clock_t` が 32 ビットなので、`si_pid` は 12 バイト目から並ぶ。
/// この libc の siginfo_t は `clock_t` が 64 ビットで、共用体が 8 バイト境界の 16 バイト目から始まる）
struct linux_siginfo {
  int32_t signo, error, code;
  int32_t pid;
  uint32_t uid;
  int32_t status;
  int32_t pad[26];
};

_Static_assert(sizeof(struct linux_siginfo) == 128, "siginfo_t is 128 bytes");

/// カーネルの waitid。答えをこの libc の siginfo_t に写す
static long kernel_waitid(int type, int id, siginfo_t *info, int options) {
  struct linux_siginfo raw = {0};
  long r = __bos_syscall5(__BOS_SYS_waitid, type, id, __bos_ptr(&raw), options, 0);
  if (r >= 0 && info) {
    memset(info, 0, sizeof *info);
    info->si_signo = raw.signo;
    info->si_errno = raw.error;
    info->si_code = raw.code;
    info->si_pid = raw.pid;
    info->si_uid = raw.uid;
    info->si_status = raw.status;
  }
  return r;
}

pid_t waitpid(pid_t pid, int *status, int options) {
  siginfo_t info;
  int type = pid < -1 ? BOS_P_PGID : pid == -1 ? BOS_P_ALL : pid == 0 ? BOS_P_PGID : BOS_P_PID;
  int id = pid < -1 ? -pid : pid == -1 ? 0 : pid;
  memset(&info, 0, sizeof info);
  long r = kernel_waitid(type, id, &info, WEXITED | (options & WNOHANG));
  if (r < 0)
    return __browseros_syscall_ret(r);
  if (info.si_pid == 0)
    return 0;
  if (status)
    *status = info.si_code == CLD_EXITED ? (info.si_status & 0xff) << 8 : info.si_status & 0x7f;
  // 子が終わる前に MAP_SHARED の写しへ書いたものを取り込む（waitpid はメモリを同期する。→ memory_sync.c）
  __browseros_memory_sync();
  return info.si_pid;
}

pid_t wait(int *status) { return waitpid(-1, status, 0); }

int waitid(idtype_t type, id_t id, siginfo_t *info, int options) {
  long r = kernel_waitid(type, id, info, options);
  if (r >= 0 && info->si_pid != 0)
    __browseros_memory_sync();
  return __browseros_syscall_ret(r);
}

/// CPU 時間などの使用量は数えていない（カーネルが子の CPU 時間を親へ渡していない）ので 0 で返す
pid_t wait4(pid_t pid, int *status, int options, struct rusage *usage) {
  pid_t child = waitpid(pid, status, options);
  if (child > 0 && usage)
    memset(usage, 0, sizeof *usage);
  return child;
}

pid_t wait3(int *status, int options, struct rusage *usage) {
  return wait4(-1, status, options, usage);
}
