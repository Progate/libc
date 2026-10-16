#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/descriptor_table.h>
#ifdef __wasip1__
// BrowserOS: dup は同じ開いたファイルの記述を指す fd を作る（位置・O_NONBLOCK を共有する）。→ browseros/host.h
#include <browseros/libc.h>
#include <browseros/linux.h>

// dup で作った番号へ、ソケットとして覚えていたことを写す（ソケットを使わないプログラムには socket.c が入らない）
static void copy_socket(int fd, int newfd) {
  if (__browseros_socket_copy)
    __browseros_socket_copy(fd, newfd);
}
#endif

int dup(int fd) {
#ifdef __wasip1__
  int newfd = __browseros_syscall_ret(__bos_syscall1(__BOS_SYS_dup, fd));
  if (newfd >= 0)
    copy_socket(fd, newfd);
  return newfd;
#else
  return descriptor_table_dup(fd, DUP_OP_DUP, 0);
#endif
}

int dup2(int fd, int newfd) {
#ifdef __wasip1__
  // dup2(2) は同じ番号なら、開いているかだけを確かめて何もしない。riscv32 の Linux に dup2 は無く、dup3 で書く
  if (fd == newfd)
    return __browseros_syscall_ret(__bos_syscall3(__BOS_SYS_fcntl64, fd, __BOS_F_GETFD, 0)) < 0 ? -1 : newfd;
  if (__browseros_syscall_ret(__bos_syscall3(__BOS_SYS_dup3, fd, newfd, 0)) < 0)
    return -1;
  copy_socket(fd, newfd);
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
  // dup3 は同じ番号を渡されたら EINVAL（カーネルが答える）
  if (__browseros_syscall_ret(__bos_syscall3(__BOS_SYS_dup3, fd, newfd, flags & O_CLOEXEC ? __BOS_O_CLOEXEC : 0)) < 0)
    return -1;
  copy_socket(fd, newfd);
  return newfd;
#else
  return descriptor_table_dup(fd, DUP_OP_DUP3, newfd);
#endif
}

weak_alias(__dup3, dup3);
