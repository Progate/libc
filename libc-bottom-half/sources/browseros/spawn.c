// posix_spawn(3) / posix_spawnp(3) と file actions（musl の src/process/posix_spawn.c と同じ形）。
//
// 子は clone(2) の `CLONE_VM | CLONE_VFORK` で作る（→ clone.c）。子は親のメモリの上で、親を止めたまま走り、シグナルの
// 動作を既定に戻し、attr（`POSIX_SPAWN_SETSID`・`SETPGROUP`・`RESETIDS`・`SETSIGMASK`・`SETSIGDEF`）と file actions を
// 本物の syscall で当ててから execve(2) する。子の段で失敗したら、その errno を親と共有のメモリに置いて 127 で終わり、
// 親はその子を回収して errno を返す（musl はパイプで返すが、ここでは同じメモリなので変数で返す）。
//
// posix_spawnp は、子の中で呼んだプロセスの PATH を引いて、`/` のあるパスで 1 つずつ試す（execvpe。musl と同じ）。

#define _GNU_SOURCE
#include <browseros/linux.h>
#include <browseros/libc.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <spawn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

enum { FDOP_CLOSE = 1, FDOP_DUP2, FDOP_OPEN, FDOP_CHDIR, FDOP_FCHDIR };

/// file actions の 1 つ。`posix_spawn_file_actions_t.__actions` は最後の 1 つを指し、`prev` で前へ辿る（musl と同じ形）
struct fdop {
  struct fdop *next, *prev;
  int cmd, fd, srcfd, oflag;
  mode_t mode;
  char path[];
};

int posix_spawn_file_actions_init(posix_spawn_file_actions_t *fa) {
  fa->__actions = 0;
  return 0;
}

int posix_spawn_file_actions_destroy(posix_spawn_file_actions_t *fa) {
  struct fdop *op = fa->__actions, *next;
  while (op) {
    next = op->prev;
    free(op);
    op = next;
  }
  return 0;
}

static void append(posix_spawn_file_actions_t *fa, struct fdop *op) {
  op->next = 0;
  if ((op->prev = fa->__actions))
    op->prev->next = op;
  fa->__actions = op;
}

int posix_spawn_file_actions_addclose(posix_spawn_file_actions_t *fa, int fd) {
  if (fd < 0)
    return EBADF;
  struct fdop *op = malloc(sizeof *op);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_CLOSE;
  op->fd = fd;
  append(fa, op);
  return 0;
}

int posix_spawn_file_actions_adddup2(posix_spawn_file_actions_t *fa, int srcfd, int fd) {
  if (srcfd < 0 || fd < 0)
    return EBADF;
  struct fdop *op = malloc(sizeof *op);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_DUP2;
  op->srcfd = srcfd;
  op->fd = fd;
  append(fa, op);
  return 0;
}

int posix_spawn_file_actions_addopen(posix_spawn_file_actions_t *restrict fa, int fd,
                                     const char *restrict path, int flags, mode_t mode) {
  if (fd < 0)
    return EBADF;
  size_t len = strlen(path) + 1;
  struct fdop *op = malloc(sizeof *op + len);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_OPEN;
  op->fd = fd;
  op->oflag = flags;
  op->mode = mode;
  memcpy(op->path, path, len);
  append(fa, op);
  return 0;
}

int posix_spawn_file_actions_addchdir_np(posix_spawn_file_actions_t *restrict fa,
                                         const char *restrict path) {
  size_t len = strlen(path) + 1;
  struct fdop *op = malloc(sizeof *op + len);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_CHDIR;
  op->fd = -1;
  memcpy(op->path, path, len);
  append(fa, op);
  return 0;
}

int posix_spawn_file_actions_addfchdir_np(posix_spawn_file_actions_t *fa, int fd) {
  if (fd < 0)
    return EBADF;
  struct fdop *op = malloc(sizeof *op);
  if (!op)
    return ENOMEM;
  op->cmd = FDOP_FCHDIR;
  op->fd = fd;
  append(fa, op);
  return 0;
}

int posix_spawnattr_init(posix_spawnattr_t *attr) {
  *attr = (posix_spawnattr_t){0};
  return 0;
}

int posix_spawnattr_destroy(posix_spawnattr_t *attr) {
  (void)attr;
  return 0;
}

int posix_spawnattr_setflags(posix_spawnattr_t *attr, short flags) {
  const unsigned all = POSIX_SPAWN_RESETIDS | POSIX_SPAWN_SETPGROUP | POSIX_SPAWN_SETSIGDEF |
                       POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSCHEDPARAM |
                       POSIX_SPAWN_SETSCHEDULER | POSIX_SPAWN_USEVFORK | POSIX_SPAWN_SETSID;
  if ((unsigned)flags & ~all)
    return EINVAL;
  attr->__flags = flags;
  return 0;
}

int posix_spawnattr_getflags(const posix_spawnattr_t *restrict attr, short *restrict flags) {
  *flags = attr->__flags;
  return 0;
}

int posix_spawnattr_setpgroup(posix_spawnattr_t *attr, pid_t pgrp) {
  attr->__pgrp = pgrp;
  return 0;
}

int posix_spawnattr_getpgroup(const posix_spawnattr_t *restrict attr, pid_t *restrict pgrp) {
  *pgrp = attr->__pgrp;
  return 0;
}

int posix_spawnattr_setsigmask(posix_spawnattr_t *restrict attr, const sigset_t *restrict mask) {
  attr->__mask = *mask;
  return 0;
}

int posix_spawnattr_getsigmask(const posix_spawnattr_t *restrict attr, sigset_t *restrict mask) {
  *mask = attr->__mask;
  return 0;
}

int posix_spawnattr_setsigdefault(posix_spawnattr_t *restrict attr, const sigset_t *restrict def) {
  attr->__def = *def;
  return 0;
}

int posix_spawnattr_getsigdefault(const posix_spawnattr_t *restrict attr, sigset_t *restrict def) {
  *def = attr->__def;
  return 0;
}

int posix_spawnattr_setschedparam(posix_spawnattr_t *restrict attr,
                                  const struct sched_param *restrict param) {
  (void)attr;
  (void)param;
  return 0;
}

int posix_spawnattr_getschedparam(const posix_spawnattr_t *restrict attr,
                                  struct sched_param *restrict param) {
  (void)attr;
  (void)param;
  return 0;
}

int posix_spawnattr_setschedpolicy(posix_spawnattr_t *attr, int policy) {
  attr->__pol = policy;
  return 0;
}

int posix_spawnattr_getschedpolicy(const posix_spawnattr_t *restrict attr, int *restrict policy) {
  *policy = attr->__pol;
  return 0;
}

#define SYS_clone 220
#define CLONE_VM 0x100
#define CLONE_VFORK 0x4000
#define SIG_SETMASK_ 2
#define SIG_BLOCK_ 0

int __clone(int (*fn)(void *), void *stack, int flags, void *arg, ...);

/// sigset_t を u64 にする（シグナル n はビット n-1。カーネルの rt_sigprocmask・rt_sigaction の並び）
static uint64_t mask_of_set(const sigset_t *set) {
  uint64_t mask = 0;
  for (int sig = 1; sig < _NSIG && sig <= 64; sig++)
    if (sigismember(set, sig))
      mask |= (uint64_t)1 << (sig - 1);
  return mask;
}

/// カーネルの struct sigaction（riscv32 の Linux。SA_RESTORER は無い）
struct kernel_sigaction {
  uint32_t handler;
  uint32_t flags;
  uint64_t mask;
};

/// 子へ渡すもの。子が書いた `error` は親から見える（CLONE_VM）
struct spawn_args {
  const char *path;
  const posix_spawn_file_actions_t *fa;
  const posix_spawnattr_t *attr;
  char *const *argv;
  char *const *envp;
  uint64_t oldmask;
  int search;
  int error;
};

/// 子の段。ここで失敗したら errno を置いて 127 で終わる
static int child(void *opaque) {
  struct spawn_args *args = opaque;
  const posix_spawnattr_t *attr = args->attr;
  short flags = attr ? attr->__flags : 0;
  long r = 0;

  /*
   * シグナルの扱いは、マスクを開ける前に既定か無視にしておく。開けたあとで親のハンドラが子の中で走ると、親と共有の
   * メモリを壊す（musl と同じ）。SETSIGDEF に挙げたものは既定に戻す
   */
  for (int sig = 1; sig < _NSIG && sig <= 64; sig++) {
    struct kernel_sigaction sa = {0};
    if ((flags & POSIX_SPAWN_SETSIGDEF) && sigismember(&attr->__def, sig)) {
      sa.handler = 0;
    } else {
      struct kernel_sigaction old;
      if (__bos_syscall4(__BOS_SYS_rt_sigaction, sig, 0, __bos_ptr(&old), 8) < 0)
        continue;
      if (old.handler == 0 || old.handler == 1)
        continue;
      sa.handler = 0;
    }
    __bos_syscall4(__BOS_SYS_rt_sigaction, sig, __bos_ptr(&sa), 0, 8);
  }

  if ((flags & POSIX_SPAWN_SETSID) && (r = __bos_syscall0(__BOS_SYS_setsid)) < 0)
    goto fail;
  if ((flags & POSIX_SPAWN_SETPGROUP) && (r = __bos_syscall2(__BOS_SYS_setpgid, 0, attr->__pgrp)) < 0)
    goto fail;
  // 利用者はプロセスごとに 1 つなので、実の id へ戻すことはいつも通る（RESETIDS）
  if (flags & POSIX_SPAWN_RESETIDS) {
    // 何もしない
  }

  if (args->fa && args->fa->__actions) {
    struct fdop *op = args->fa->__actions;
    // 最後の 1 つを指しているので、最初の 1 つへ戻ってから順に当てる
    while (op->prev)
      op = op->prev;
    for (; op; op = op->next) {
      switch (op->cmd) {
      case FDOP_CLOSE:
        // 閉じている fd を閉じても失敗にしない（posix_spawn_file_actions_addclose）
        __bos_syscall1(57, op->fd);
        break;
      case FDOP_DUP2:
        if (op->srcfd != op->fd) {
          if ((r = __bos_syscall3(__BOS_SYS_dup3, op->srcfd, op->fd, 0)) < 0)
            goto fail;
        } else {
          // 同じ番号なら close-on-exec を落とす
          if ((r = __bos_syscall2(__BOS_SYS_fcntl64, op->fd, F_GETFD)) < 0 ||
              (r = __bos_syscall3(__BOS_SYS_fcntl64, op->fd, F_SETFD, r & ~FD_CLOEXEC)) < 0)
            goto fail;
        }
        break;
      case FDOP_OPEN: {
        long fd = __bos_syscall4(__BOS_SYS_openat, AT_FDCWD, __bos_ptr(op->path), op->oflag, op->mode);
        if ((r = fd) < 0)
          goto fail;
        if (fd != op->fd) {
          if ((r = __bos_syscall3(__BOS_SYS_dup3, fd, op->fd, 0)) < 0)
            goto fail;
          __bos_syscall1(57, fd);
        }
        break;
      }
      case FDOP_CHDIR:
        if ((r = __bos_syscall1(__BOS_SYS_chdir, __bos_ptr(op->path))) < 0)
          goto fail;
        break;
      case FDOP_FCHDIR:
        if ((r = __bos_syscall1(__BOS_SYS_fchdir, op->fd)) < 0)
          goto fail;
        break;
      }
    }
  }

  uint64_t mask = (flags & POSIX_SPAWN_SETSIGMASK) ? mask_of_set(&attr->__mask) : args->oldmask;
  __bos_syscall4(__BOS_SYS_rt_sigprocmask, SIG_SETMASK_, __bos_ptr(&mask), 0, 8);

  if (args->search)
    execvpe(args->path, args->argv, args->envp);
  else
    execve(args->path, args->argv, args->envp);
  r = -errno;

fail:
  args->error = (int)-r;
  _Exit(127);
}

/// 起こす。`search` なら `file` に `/` が無いとき、子の中で呼んだプロセスの PATH から探す
static int spawn(pid_t *restrict pid, const char *restrict file, const posix_spawn_file_actions_t *fa,
                 const posix_spawnattr_t *restrict attr, char *const argv[restrict], char *const envp[restrict],
                 int search) {
  struct spawn_args args = {
      .path = file,
      .fa = fa,
      .attr = attr,
      .argv = argv,
      .envp = envp ? envp : environ,
      .search = search,
      .error = 0,
  };
  // 子の段のあいだ、親のハンドラが走らないように、すべてのシグナルを塞いでおく（子は終わりに元へ戻す）
  uint64_t all = ~(uint64_t)0;
  __bos_syscall4(__BOS_SYS_rt_sigprocmask, SIG_BLOCK_, __bos_ptr(&all), __bos_ptr(&args.oldmask), 8);
  // 子が始まる前に、MAP_SHARED の写しに書いたものを出しておく（子は写しをファイルから読む。→ memory_sync.c）
  __browseros_memory_sync();
  int child_pid = __clone(child, 0, CLONE_VM | CLONE_VFORK | SIGCHLD, &args);
  int error = child_pid < 0 ? -child_pid : args.error;
  // 子の段で失敗した子は、ここで回収する（その子は 127 で終わっている）
  if (child_pid > 0 && error) {
    // カーネルの siginfo_t（riscv32 の Linux。128 バイト）を受ける場所
    uint64_t info[16];
    __bos_syscall5(__BOS_SYS_waitid, P_PID, child_pid, __bos_ptr(info), WEXITED, 0);
  }
  if (!error && pid)
    *pid = child_pid;
  __bos_syscall4(__BOS_SYS_rt_sigprocmask, SIG_SETMASK_, __bos_ptr(&args.oldmask), 0, 8);
  return error;
}

int posix_spawn(pid_t *restrict pid, const char *restrict path, const posix_spawn_file_actions_t *fa,
                const posix_spawnattr_t *restrict attr, char *const argv[restrict], char *const envp[restrict]) {
  return spawn(pid, path, fa, attr, argv, envp, 0);
}

int posix_spawnp(pid_t *restrict pid, const char *restrict file, const posix_spawn_file_actions_t *fa,
                 const posix_spawnattr_t *restrict attr, char *const argv[restrict], char *const envp[restrict]) {
  return spawn(pid, file, fa, attr, argv, envp, 1);
}
