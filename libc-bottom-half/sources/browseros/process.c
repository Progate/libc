// プロセスの番号・kill(2)・sigqueue(3)・umask(2)。
//
// pid は BrowserOS のカーネルが振ったもので、`ps` や親の waitpid と同じ番号である（wasi-libc の
// wasi-emulated-getpid のような固定値ではない）。umask はプロセスごとにカーネルが持ち、open(2) / mkdir(2) が
// 新しく作るファイルの権限から落とす（既定は Linux と同じ 022）。

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

int kill(pid_t pid, int sig) { return __browseros_syscall_ret(__bos_syscall2(__BOS_SYS_kill, pid, sig)); }

/// 値は届かない（カーネルは siginfo の値を運ばない）
int sigqueue(pid_t pid, int sig, union sigval value) {
  (void)value;
  return kill(pid, sig);
}

/// umask(2)。umask はプロセスごとにカーネルが持ち、子へ継ぐ
mode_t umask(mode_t mask) { return __bos_syscall1(__BOS_SYS_umask, mask & 0777); }

/// open(2) / mkdir(2) が新しく作るものに付ける権限（`mode & ~umask`）
mode_t __browseros_creation_mode(mode_t mode) {
  mode_t mask = umask(0);
  umask(mask);
  return mode & ~mask & 07777;
}

pid_t getpgid(pid_t pid) { return __browseros_syscall_ret(__bos_syscall1(__BOS_SYS_getpgid, pid)); }

pid_t getpgrp(void) { return getpgid(0); }

int setpgid(pid_t pid, pid_t pgid) { return __browseros_syscall_ret(__bos_syscall2(__BOS_SYS_setpgid, pid, pgid)); }

pid_t setpgrp(void) { return setpgid(0, 0); }

pid_t setsid(void) { return __browseros_syscall_ret(__bos_syscall0(__BOS_SYS_setsid)); }

pid_t getsid(pid_t pid) { return __browseros_syscall_ret(__bos_syscall1(__BOS_SYS_getsid, pid)); }
