// waitpid(2) / wait(2) / wait3 / wait4 / waitid(2)。子が終わるのを待つのはカーネルの waitid（riscv32 の Linux には
// wait4 が無く、waitid だけがある。musl の emulate_wait4.c と同じく、waitpid を waitid で書く）。
//
// `pid` が 0 なら自分のグループの子、-pgid ならそのグループの子を待つ。WUNTRACED・WCONTINUED なら止まった子・続けた子も
// 報告する（waitid の WSTOPPED・WCONTINUED）。wait4 の `rusage` はカーネルの waitid の 5 つ目の引数が書く。

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

/// カーネルの struct rusage（riscv32 の Linux。timeval は long 2 つ）
struct linux_rusage {
  int32_t utime_sec, utime_usec, stime_sec, stime_usec;
  int32_t rest[14];
};

_Static_assert(sizeof(struct linux_rusage) == 72, "struct rusage is 72 bytes");

/// カーネルの struct rusage をこの libc の struct rusage に写す
void __browseros_rusage_of(const void *kernel, struct rusage *usage) {
  const struct linux_rusage *raw = kernel;
  memset(usage, 0, sizeof *usage);
  usage->ru_utime.tv_sec = raw->utime_sec;
  usage->ru_utime.tv_usec = raw->utime_usec;
  usage->ru_stime.tv_sec = raw->stime_sec;
  usage->ru_stime.tv_usec = raw->stime_usec;
  long *fields = &usage->ru_maxrss;
  for (int i = 0; i < 14; i++)
    fields[i] = raw->rest[i];
}

/// カーネルの waitid。答えをこの libc の siginfo_t と struct rusage に写す
static long kernel_waitid(int type, int id, siginfo_t *info, int options, struct rusage *usage) {
  struct linux_siginfo raw = {0};
  struct linux_rusage raw_usage;
  long r = __bos_syscall5(__BOS_SYS_waitid, type, id, __bos_ptr(&raw), options, usage ? __bos_ptr(&raw_usage) : 0);
  if (r >= 0 && usage)
    __browseros_rusage_of(&raw_usage, usage);
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

/// waitpid と wait4。WUNTRACED は waitid の WSTOPPED と同じ値で、WCONTINUED は同じ名前の値
static pid_t wait_child(pid_t pid, int *status, int options, struct rusage *usage) {
  siginfo_t info;
  if (options & ~(WNOHANG | WUNTRACED | WCONTINUED | __WNOTHREAD | __WALL | __WCLONE)) {
    errno = EINVAL;
    return -1;
  }
  int type = pid < -1 ? BOS_P_PGID : pid == -1 ? BOS_P_ALL : pid == 0 ? BOS_P_PGID : BOS_P_PID;
  int id = pid < -1 ? -pid : pid == -1 ? 0 : pid;
  memset(&info, 0, sizeof info);
  long r = kernel_waitid(type, id, &info, WEXITED | (options & (WNOHANG | WUNTRACED | WCONTINUED)), usage);
  if (r < 0)
    return __browseros_syscall_ret(r);
  if (info.si_pid == 0)
    return 0;
  if (status) {
    switch (info.si_code) {
    case CLD_EXITED:
      *status = (info.si_status & 0xff) << 8;
      break;
    case CLD_STOPPED:
      *status = (info.si_status & 0xff) << 8 | 0x7f;
      break;
    case CLD_CONTINUED:
      *status = 0xffff;
      break;
    default:
      *status = info.si_status & 0x7f;
    }
  }
  // 子が終わる前に MAP_SHARED の写しへ書いたものを取り込む（waitpid はメモリを同期する。→ memory_sync.c）
  __browseros_memory_sync();
  return info.si_pid;
}

pid_t waitpid(pid_t pid, int *status, int options) { return wait_child(pid, status, options, 0); }

pid_t wait(int *status) { return waitpid(-1, status, 0); }

int waitid(idtype_t type, id_t id, siginfo_t *info, int options) {
  long r = kernel_waitid(type, id, info, options, 0);
  if (r >= 0 && info->si_pid != 0)
    __browseros_memory_sync();
  return __browseros_syscall_ret(r);
}

/// `usage` には、回収した子とその子が回収した子孫が使った CPU 時間が入る（カーネルの waitid が書く）
pid_t wait4(pid_t pid, int *status, int options, struct rusage *usage) {
  if (usage)
    memset(usage, 0, sizeof *usage);
  return wait_child(pid, status, options, usage);
}

pid_t wait3(int *status, int options, struct rusage *usage) {
  return wait4(-1, status, options, usage);
}
