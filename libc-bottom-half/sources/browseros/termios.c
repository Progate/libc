// 端末: termios(3)（tcgetattr・tcsetattr ほか）・tcgetwinsize・isatty(3)。
//
// 端末の設定（struct termios）は端末ごとにカーネルが持ち、どのプロセスから見ても同じである（Linux と同じ）。
// libc はカーネルの ioctl(2)（TCGETS・TCSETS・TCSETSW・TCSETSF・TCSBRK・TCXONC・TCFLSH）を呼ぶだけで、musl の
// src/termios と同じ書き方である。カーネルの struct termios（riscv32 の Linux。c_cc は 19 個で 36 バイト）は、この
// libc の struct termios（c_cc が 32 個と速度の欄が続く）の先頭と同じ並びなので、先頭をそのまま受け渡す。
//
// isatty は「その fd に TIOCGWINSZ できるか」で決める（musl と同じ）。wasi-libc の isatty は WASI の種別
// （文字デバイス）で決めるので、パイプやファイルに向いた標準出力も端末に見えてしまう。

#define _GNU_SOURCE
#include <browseros/linux.h>
#include <errno.h>
#include <stddef.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/// カーネルの struct termios の大きさ（c_iflag〜c_lflag・c_line・c_cc[19]）
#define KERNEL_TERMIOS_BYTES 36

_Static_assert(offsetof(struct termios, c_cc) == 17, "c_cc follows c_line as in the kernel struct termios");

static long tty_ioctl(int fd, int request, const void *arg) {
  return __bos_syscall3(__BOS_SYS_ioctl, fd, request, __bos_ptr(arg));
}

int tcgetattr(int fd, struct termios *out) {
  unsigned char raw[KERNEL_TERMIOS_BYTES];
  long r = tty_ioctl(fd, __BOS_TCGETS, raw);
  if (r < 0)
    return __browseros_syscall_ret(r);
  memset(out, 0, sizeof *out);
  memcpy(out, raw, sizeof raw);
  return 0;
}

int tcsetattr(int fd, int actions, const struct termios *in) {
  if (actions < 0 || actions > 2) {
    errno = EINVAL;
    return -1;
  }
  unsigned char raw[KERNEL_TERMIOS_BYTES];
  memcpy(raw, in, sizeof raw);
  return __browseros_syscall_ret(tty_ioctl(fd, __BOS_TCSETS + actions, raw));
}

int tcdrain(int fd) { return __browseros_syscall_ret(__bos_syscall3(__BOS_SYS_ioctl, fd, __BOS_TCSBRK, 1)); }

int tcflush(int fd, int queue) { return __browseros_syscall_ret(__bos_syscall3(__BOS_SYS_ioctl, fd, __BOS_TCFLSH, queue)); }

int tcflow(int fd, int action) { return __browseros_syscall_ret(__bos_syscall3(__BOS_SYS_ioctl, fd, __BOS_TCXONC, action)); }

int tcsendbreak(int fd, int duration) {
  (void)duration;
  return __browseros_syscall_ret(__bos_syscall3(__BOS_SYS_ioctl, fd, __BOS_TCSBRK, 0));
}

/// 端末のセッション。この OS の端末はそれを制御端末にしているセッションを ioctl で答えないので、端末なら自分の
/// セッションを返す（端末を持つのは、それを制御端末にしているセッションのプロセスである）
pid_t tcgetsid(int fd) {
  struct termios ignored;
  if (tcgetattr(fd, &ignored) < 0)
    return -1;
  return getsid(0);
}

int tcgetwinsize(int fd, struct winsize *size) { return ioctl(fd, TIOCGWINSZ, size); }

int tcsetwinsize(int fd, const struct winsize *size) { return ioctl(fd, TIOCSWINSZ, size); }

int __isatty(int fd) {
  struct winsize size;
  long r = tty_ioctl(fd, __BOS_TIOCGWINSZ, &size);
  if (r == 0)
    return 1;
  errno = r == -EBADF ? EBADF : ENOTTY;
  return 0;
}

__attribute__((__weak__, __alias__("__isatty"))) int isatty(int fd);
