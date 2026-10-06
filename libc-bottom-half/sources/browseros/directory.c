// fchdir(2)。
//
// 作業ディレクトリの持ち主は libc（wasi-libc は cwd を自分で持ち、相対パスを preopen からの道に直す）で、fd が
// どのディレクトリを指しているかを知っているのはカーネルである。カーネルにそのディレクトリのいまの絶対パスを聞き、
// libc の chdir でそこへ移る（→ browser-os の src/wasi/fs.ts の fchdir）。名前を失ったディレクトリ（消された・
// 見えない所へ移された）には移れないので ENOENT になる

#include <browseros/host.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>

int fchdir(int fd) {
  char path[PATH_MAX];
  uint32_t length = 0;
  int32_t error = __browseros_fchdir(fd, path, sizeof path - 1, &length);
  if (error != 0) {
    errno = error;
    return -1;
  }
  path[length] = '\0';
  return chdir(path);
}
