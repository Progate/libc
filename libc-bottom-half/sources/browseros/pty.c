// 擬似端末: posix_openpt(3)・grantpt(3)・unlockpt(3)・ptsname(3)。musl の src/misc/pty.c と同じく、/dev/ptmx を開き、
// 従の番号（TIOCGPTN）と錠（TIOCSPTLCK）をカーネルの ioctl(2) で扱う（→ browser-os の src/wasi/pty.ts）

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>

int posix_openpt(int flags) {
  int r = open("/dev/ptmx", flags);
  if (r < 0 && errno == ENOSPC)
    errno = EAGAIN;
  return r;
}

int grantpt(int fd) {
  (void)fd;
  return 0;
}

int unlockpt(int fd) {
  int unlock = 0;
  return ioctl(fd, TIOCSPTLCK, &unlock);
}

int ptsname_r(int fd, char *buf, size_t len) {
  unsigned int pty;
  if (ioctl(fd, TIOCGPTN, &pty) < 0)
    return errno;
  if (!buf)
    len = 0;
  if ((size_t)snprintf(buf, len, "/dev/pts/%u", pty) >= len)
    return ERANGE;
  return 0;
}

char *ptsname(int fd) {
  static char buf[9 + sizeof(int) * 3 + 1];
  int err = ptsname_r(fd, buf, sizeof buf);
  if (err) {
    errno = err;
    return 0;
  }
  return buf;
}
