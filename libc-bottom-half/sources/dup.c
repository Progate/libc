#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/descriptor_table.h>
#ifdef __wasip1__
// BrowserOS: dup は同じ開いたファイルの記述を指す fd を作る（位置・O_NONBLOCK を共有する）。→ browseros/host.h
#include <browseros/host.h>
#endif

int dup(int fd) {
#ifdef __wasip1__
  int32_t newfd;
  int32_t error = __browseros_dup(fd, 0, &newfd);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return newfd;
#else
  return descriptor_table_dup(fd, DUP_OP_DUP, 0);
#endif
}

int dup2(int fd, int newfd) {
#ifdef __wasip1__
  int32_t error = __browseros_dup2(fd, newfd);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return newfd;
#else
  return descriptor_table_dup(fd, DUP_OP_DUP2, newfd);
#endif
}

int __dup3(int fd, int newfd, int flags) {
  if (flags & ~O_CLOEXEC) {
    errno = EINVAL;
    return -1;
  }
#ifdef __wasip1__
  // dup3 は同じ番号を渡されたら EINVAL（dup2 は何もせず成功する）
  if (fd == newfd) {
    errno = EINVAL;
    return -1;
  }
  return dup2(fd, newfd);
#else
  return descriptor_table_dup(fd, DUP_OP_DUP3, newfd);
#endif
}

weak_alias(__dup3, dup3);
