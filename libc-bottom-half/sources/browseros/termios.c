// 端末: termios(3)（tcgetattr・tcsetattr ほか）・tcgetwinsize・isatty(3)。
//
// 端末そのもの（行編集・エコー・Ctrl+C を SIGINT にするか）は BrowserOS のカーネルが持ち、ホスト関数
// （browser_os_tty）で ICANON・ECHO・ISIG の 3 つだけをやり取りする（→ browser-os の src/wasi/tty.ts）。
// struct termios のほかの欄はカーネルが使わないので、最後に設定されたものを libc が覚えて返す。
//
// isatty は「その fd に tcgetattr できるか」で決める（musl と同じ）。wasi-libc の isatty は WASI の
// 種別（文字デバイス）で決めるので、パイプやファイルに向いた標準出力も端末に見え、stdio が行ごとに
// 書き出してしまう（Unix ではパイプへの出力はまとめて書かれる）。

#define _GNU_SOURCE
#include <browseros/host.h>
#include <errno.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/// カーネルとやり取りする 3 つ（i32 の並び。→ browser-os の wasi/tty.ts）
struct tty_state {
  int32_t raw, echo, isig;
};

/// 最後に設定された termios。まだ設定されていなければ Linux の端末の初期値（`stty sane`）から始める
static struct termios current;
static int current_valid;

static void sane(struct termios *t) {
  memset(t, 0, sizeof *t);
  t->c_iflag = BRKINT | ICRNL | IXON | IUTF8;
  t->c_oflag = OPOST | ONLCR;
  t->c_cflag = CS8 | CREAD | B38400;
  t->c_lflag = ISIG | ICANON | ECHO | ECHOE | ECHOK | ECHOCTL | ECHOKE | IEXTEN;
  t->c_cc[VINTR] = 0x03;
  t->c_cc[VQUIT] = 0x1c;
  t->c_cc[VERASE] = 0x7f;
  t->c_cc[VKILL] = 0x15;
  t->c_cc[VEOF] = 0x04;
  t->c_cc[VSTART] = 0x11;
  t->c_cc[VSTOP] = 0x13;
  t->c_cc[VSUSP] = 0x1a;
  t->c_cc[VMIN] = 1;
  t->c_cc[VTIME] = 0;
}

static int fail(int32_t error) {
  errno = error;
  return -1;
}

int tcgetattr(int fd, struct termios *out) {
  struct tty_state state;
  int32_t error = __browseros_tcgetattr(fd, (int32_t *)&state);
  if (error)
    return fail(error);
  if (!current_valid) {
    sane(&current);
    current_valid = 1;
  }
  // カーネルが持っている 3 つだけは、いまの値で上書きする（別のプロセスが変えていることがある）
  current.c_lflag = state.raw ? current.c_lflag & ~(tcflag_t)ICANON : current.c_lflag | ICANON;
  current.c_lflag = state.echo ? current.c_lflag | ECHO : current.c_lflag & ~(tcflag_t)ECHO;
  current.c_lflag = state.isig ? current.c_lflag | ISIG : current.c_lflag & ~(tcflag_t)ISIG;
  *out = current;
  return 0;
}

int tcsetattr(int fd, int actions, const struct termios *in) {
  if (actions != TCSANOW && actions != TCSADRAIN && actions != TCSAFLUSH)
    return fail(EINVAL);
  /*
   * raw かどうかは ICANON で決め、ECHO では決めない。ICANON を落としたまま ECHO を残すプログラム
   * （vim の `:!` の前後）があり、「エコーが消えたら raw」にすると行編集が戻らない
   */
  struct tty_state state = {
      .raw = !(in->c_lflag & ICANON),
      .echo = !!(in->c_lflag & ECHO),
      .isig = !!(in->c_lflag & ISIG),
  };
  int32_t error = __browseros_tcsetattr(fd, actions, (const int32_t *)&state);
  if (error)
    return fail(error);
  current = *in;
  current_valid = 1;
  return 0;
}

/// 端末かどうかだけを確かめる（出力を待つ列も、止める流れも、送るブレークも、この端末には無い）
static int tty_only(int fd) {
  struct termios ignored;
  return tcgetattr(fd, &ignored);
}

int tcdrain(int fd) { return tty_only(fd); }

int tcflush(int fd, int queue) {
  if (queue != TCIFLUSH && queue != TCOFLUSH && queue != TCIOFLUSH)
    return fail(EINVAL);
  return tty_only(fd);
}

int tcflow(int fd, int action) {
  if (action != TCOOFF && action != TCOON && action != TCIOFF && action != TCION)
    return fail(EINVAL);
  return tty_only(fd);
}

int tcsendbreak(int fd, int duration) {
  (void)duration;
  return tty_only(fd);
}

/// どのプロセスも自分だけのセッションに居るものとして扱う（→ process.c）
pid_t tcgetsid(int fd) {
  if (tty_only(fd) < 0)
    return -1;
  return getpid();
}

int tcgetwinsize(int fd, struct winsize *size) { return ioctl(fd, TIOCGWINSZ, size); }

int tcsetwinsize(int fd, const struct winsize *size) { return ioctl(fd, TIOCSWINSZ, size); }

int __isatty(int fd) {
  struct tty_state state;
  int32_t error = __browseros_tcgetattr(fd, (int32_t *)&state);
  if (error) {
    errno = error;
    return 0;
  }
  return 1;
}

__attribute__((__weak__, __alias__("__isatty"))) int isatty(int fd);
