// tmpfile(3) / tmpnam(3) / tempnam(3)。一時ファイルは /tmp に作る（musl と同じ）。
//
// tmpfile は作ってすぐ名前を消し、開いた fd だけを残す。BrowserOS のカーネルは名前の消えたファイルの中身を
// 開いている fd がある限り残すので（→ browser-os の fs/open-files.ts）、Unix と同じく誰からも見えない
// 一時ファイルになり、閉じれば消える。mkstemp などは musl のもの（musl/src/temp）をそのまま使う。

#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/// 名前を作り直す回数の上限（musl と同じ）
#define MAXTRIES 100

/// musl の一時ファイル名の 6 文字を埋める（→ musl/src/temp/__randname.c）
char *__randname(char *);

FILE *tmpfile(void) {
  char path[] = "/tmp/tmpfile_XXXXXX";
  int fd = mkstemp(path);
  if (fd < 0)
    return 0;
  unlink(path);
  FILE *file = fdopen(fd, "w+");
  if (!file)
    close(fd);
  return file;
}

/// まだ無い名前を `prefix` の後ろに 6 文字足して作る。見つからなければ 0
static char *unused_name(char *buffer, size_t size, const char *dir, const char *prefix) {
  int length = snprintf(buffer, size, "%s/%sXXXXXX", dir, prefix);
  if (length < 0 || (size_t)length >= size) {
    errno = ENAMETOOLONG;
    return 0;
  }
  struct stat st;
  for (int i = 0; i < MAXTRIES; i++) {
    __randname(buffer + length - 6);
    if (lstat(buffer, &st) != 0 && errno == ENOENT)
      return buffer;
  }
  errno = EEXIST;
  return 0;
}

char *tmpnam(char *buffer) {
  static char internal[L_tmpnam];
  return unused_name(buffer ? buffer : internal, L_tmpnam, P_tmpdir, "tmpnam_");
}

char *tempnam(const char *dir, const char *prefix) {
  if (!dir)
    dir = P_tmpdir;
  if (!prefix)
    prefix = "temp";
  size_t size = strlen(dir) + strlen(prefix) + 8;
  char *buffer = malloc(size);
  if (!buffer)
    return 0;
  if (!unused_name(buffer, size, dir, prefix)) {
    free(buffer);
    return 0;
  }
  return buffer;
}
