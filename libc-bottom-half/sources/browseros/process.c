// プロセスの番号・kill(2)・sigqueue(3)・umask(2)。
//
// pid は BrowserOS のカーネルが振ったもので、`ps` や親の waitpid と同じ番号である（wasi-libc の
// wasi-emulated-getpid のような固定値ではない）。umask はプロセスごとに libc が持ち、open(2) / mkdir(2) が
// 新しく作るファイルの権限から落とす（既定は Linux と同じ 022）。
//
// この OS にプロセスグループは無く、どのプロセスも自分だけのグループ（番号は自分の pid）に居るものとして扱う。

#define _GNU_SOURCE
#include <browseros/host.h>
#include <browseros/libc.h>
#include <browseros/linux.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

/// getpid(2)。カーネルが答えない（BrowserOS でないホストの）ときは 1
pid_t getpid(void) {
  long pid = __bos_syscall0(__BOS_SYS_getpid);
  return pid < 0 ? 1 : pid;
}

pid_t getppid(void) {
  long pid = __bos_syscall0(__BOS_SYS_getppid);
  return pid < 0 ? 0 : pid;
}

int kill(pid_t pid, int sig) {
  pid_t self = getpid();
  // 自分自身へ送るのは raise と同じ（ハンドラがあれば呼ぶ。→ signal.c）
  if (pid == self) {
    if (sig == 0)
      return 0;
    return raise(sig);
  }
  // 「送れる全部」（-1）は受けない。0 と -pgid はグループ全体で、自分も入っていればカーネルが保留に積むか止める
  if (pid == -1) {
    errno = EPERM;
    return -1;
  }
  return __browseros_syscall_ret(__bos_syscall2(__BOS_SYS_kill, pid, sig));
}

/// 値は届かない（別のプロセスのハンドラへは配れず、自分へは raise と同じく SI_USER で配る）
int sigqueue(pid_t pid, int sig, union sigval value) {
  (void)value;
  return kill(pid, sig);
}

static mode_t current_umask = 022;

mode_t umask(mode_t mask) {
  mode_t old = current_umask;
  current_umask = mask & 0777;
  return old;
}

/// open(2) / mkdir(2) が新しく作るものに付ける権限（`mode & ~umask`）
mode_t __browseros_creation_mode(mode_t mode) { return mode & ~current_umask & 07777; }

pid_t getpgid(pid_t pid) { return __browseros_syscall_ret(__bos_syscall1(__BOS_SYS_getpgid, pid)); }

pid_t getpgrp(void) { return getpgid(0); }

int setpgid(pid_t pid, pid_t pgid) { return __browseros_syscall_ret(__bos_syscall2(__BOS_SYS_setpgid, pid, pgid)); }

pid_t setpgrp(void) { return setpgid(0, 0); }

pid_t setsid(void) { return __browseros_syscall_ret(__bos_syscall0(__BOS_SYS_setsid)); }

pid_t getsid(pid_t pid) { return __browseros_syscall_ret(__bos_syscall1(__BOS_SYS_getsid, pid)); }
