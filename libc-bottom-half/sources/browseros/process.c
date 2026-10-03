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
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>
#include <unistd.h>

pid_t getpid(void) {
  int32_t pid;
  if (__browseros_getpid(&pid) != 0)
    return 1;
  return pid;
}

pid_t getppid(void) {
  int32_t pid;
  if (__browseros_getppid(&pid) != 0)
    return 0;
  return pid;
}

int kill(pid_t pid, int sig) {
  pid_t self = getpid();
  // 自分自身（自分のグループ）へ送るのは raise と同じ（ハンドラがあれば呼ぶ。→ signal.c）
  if (pid == self || pid == 0 || pid == -self) {
    if (sig == 0)
      return 0;
    return raise(sig);
  }
  // ほかのグループも「全部」（-1）も、送れる相手は居ない
  if (pid < 0) {
    errno = ESRCH;
    return -1;
  }
  int32_t error = __browseros_kill(pid, sig);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
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
