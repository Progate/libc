// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include <wasi/api.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>

#ifdef __wasip1__
#include <browseros/libc.h>
#include <browseros/linux.h>
#endif

#ifndef __wasip1__
#include <stddefer.h>
#include <wasi/file_utils.h>
#include <common/errors.h>
#endif

int fcntl(int fildes, int cmd, ...) {
#if defined(__wasip2__) || defined(__wasip3__)
  descriptor_table_entry_t entry;
  if (descriptor_table_get(fildes, &entry) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);
#endif

  switch (cmd) {
#if defined(__wasip1__)
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
      // BrowserOS: lowest 以上でいちばん小さい空き番号に複製する（カーネルの fcntl(2)）
      va_list ap;
      va_start(ap, cmd);
      int lowest = va_arg(ap, int);
      va_end(ap);
      int newfd = __browseros_syscall_ret(
          __bos_syscall3(__BOS_SYS_fcntl64, fildes, cmd, lowest));
      // 複製にもソケットとして覚えていたことを写す（→ browseros/socket.c）
      if (newfd >= 0 && __browseros_socket_copy)
        __browseros_socket_copy(fildes, newfd);
      return newfd;
    }
    case F_GETFD:
      // BrowserOS: close-on-exec は fd の番号ごとにカーネルが持つ
      return __browseros_syscall_ret(__bos_syscall3(__BOS_SYS_fcntl64, fildes, F_GETFD, 0));
    case F_SETFD: {
      va_list ap;
      va_start(ap, cmd);
      int flags = va_arg(ap, int);
      va_end(ap);
      return __browseros_syscall_ret(__bos_syscall3(__BOS_SYS_fcntl64, fildes, F_SETFD, flags & FD_CLOEXEC));
    }
#else
    case F_GETFD:
      // Act as if the close-on-exec flag is always set.
      return FD_CLOEXEC;
    case F_SETFD:
      // The close-on-exec flag is ignored.
      return 0;
#endif
    case F_GETFL: {

#if defined(__wasip1__)
      // BrowserOS: カーネルの fcntl(2) の F_GETFL（O_* はこの libc と同じ Linux の値）
      return __browseros_syscall_ret(__bos_syscall3(__BOS_SYS_fcntl64, fildes, F_GETFL, 0));
#elif defined(__wasip2__) || defined(__wasip3__)
      if (!entry.vtable->fcntl_getfl) {
        errno = EINVAL;
        return -1;
      }
      return entry.vtable->fcntl_getfl(entry.data);
#else
# error "Unknown WASI version"
#endif
    }
    case F_SETFL: {
      // Set new file descriptor flags.
      va_list ap;
      va_start(ap, cmd);
      int flags = va_arg(ap, int);
      va_end(ap);

#if defined(__wasip1__)
      // BrowserOS: カーネルの fcntl(2) の F_SETFL（変えられるのは O_APPEND と O_NONBLOCK。ほかのビットはカーネルが捨てる）
      if (__browseros_syscall_ret(__bos_syscall3(__BOS_SYS_fcntl64, fildes, F_SETFL, flags)) < 0)
        return -1;
#elif defined(__wasip2__) || defined(__wasip3__)
      if (!entry.vtable->fcntl_setfl) {
        errno = EINVAL;
        return -1;
      }
      return entry.vtable->fcntl_setfl(entry.data, flags);
#else
# error "Unknown WASI version"
#endif
      return 0;
    }
#ifndef __wasip1__
    case F_DUPFD:
    case F_DUPFD_CLOEXEC: {
      va_list ap;
      va_start(ap, cmd);
      int minfd = va_arg(ap, int);
      va_end(ap);
      return descriptor_table_dup(fildes, DUP_OP_DUPFD, minfd);
    }
#endif
    case F_GETLK:
    case F_SETLK:
    case F_SETLKW:
      // POSIX advisory record locking is not supported by WASI.
      errno = ENOTSUP;
      return -1;
    default:
      errno = EINVAL;
      return -1;
  }
}
