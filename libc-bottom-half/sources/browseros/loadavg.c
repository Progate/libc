// getloadavg(3)。カーネルが数えているロードアベレージ（/proc/loadavg。→ browser-os の fs/proc.ts）を読む。
// 読めなければ（/proc の無い世界）-1 を返す（glibc と同じく、得られなければ -1）

#define _GNU_SOURCE
#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

int getloadavg(double *loadavg, int nelem) {
  if (nelem < 0)
    return -1;
  if (nelem == 0)
    return 0;
  int fd = open("/proc/loadavg", O_RDONLY | O_CLOEXEC);
  if (fd < 0)
    return -1;
  char text[128];
  ssize_t length = read(fd, text, sizeof text - 1);
  close(fd);
  if (length <= 0)
    return -1;
  text[length] = '\0';
  char *cursor = text;
  int count = 0;
  for (; count < nelem && count < 3; count++) {
    char *end;
    double value = strtod(cursor, &end);
    if (end == cursor)
      break;
    loadavg[count] = value;
    cursor = end;
  }
  return count == 0 ? -1 : count;
}
