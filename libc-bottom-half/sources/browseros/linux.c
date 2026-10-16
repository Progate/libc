// Linux の syscall の入口を呼ぶ（→ browseros/linux.h）。カーネルが返す Linux の errno を、この libc の errno（WASI の値）に直す。

#include <browseros/linux.h>
#include <errno.h>
#include <sys/stat.h>

/// Linux の errno（添字）に当たるこの libc の errno。同じ名前のものを引いた（musl の arch/generic/bits/errno.h と
/// wasi-libc の __errno_values.h）。この libc に無いもの（ENOTBLK など）は EIO にする
static const unsigned char errno_of_linux[] = {
    [1] = EPERM,
    [2] = ENOENT,
    [3] = ESRCH,
    [4] = EINTR,
    [5] = EIO,
    [6] = ENXIO,
    [7] = E2BIG,
    [8] = ENOEXEC,
    [9] = EBADF,
    [10] = ECHILD,
    [11] = EAGAIN,
    [12] = ENOMEM,
    [13] = EACCES,
    [14] = EFAULT,
    [16] = EBUSY,
    [17] = EEXIST,
    [18] = EXDEV,
    [19] = ENODEV,
    [20] = ENOTDIR,
    [21] = EISDIR,
    [22] = EINVAL,
    [23] = ENFILE,
    [24] = EMFILE,
    [25] = ENOTTY,
    [26] = ETXTBSY,
    [27] = EFBIG,
    [28] = ENOSPC,
    [29] = ESPIPE,
    [30] = EROFS,
    [31] = EMLINK,
    [32] = EPIPE,
    [33] = EDOM,
    [34] = ERANGE,
    [35] = EDEADLK,
    [36] = ENAMETOOLONG,
    [37] = ENOLCK,
    [38] = ENOSYS,
    [39] = ENOTEMPTY,
    [40] = ELOOP,
    [42] = ENOMSG,
    [43] = EIDRM,
    [67] = ENOLINK,
    [71] = EPROTO,
    [72] = EMULTIHOP,
    [74] = EBADMSG,
    [75] = EOVERFLOW,
    [84] = EILSEQ,
    [88] = ENOTSOCK,
    [89] = EDESTADDRREQ,
    [90] = EMSGSIZE,
    [91] = EPROTOTYPE,
    [92] = ENOPROTOOPT,
    [93] = EPROTONOSUPPORT,
    [95] = ENOTSUP,
    [97] = EAFNOSUPPORT,
    [98] = EADDRINUSE,
    [99] = EADDRNOTAVAIL,
    [100] = ENETDOWN,
    [101] = ENETUNREACH,
    [102] = ENETRESET,
    [103] = ECONNABORTED,
    [104] = ECONNRESET,
    [105] = ENOBUFS,
    [106] = EISCONN,
    [107] = ENOTCONN,
    [110] = ETIMEDOUT,
    [111] = ECONNREFUSED,
    [113] = EHOSTUNREACH,
    [114] = EALREADY,
    [115] = EINPROGRESS,
    [116] = ESTALE,
    [122] = EDQUOT,
    [125] = ECANCELED,
    [130] = EOWNERDEAD,
    [131] = ENOTRECOVERABLE,};

long __browseros_syscall(int nr, int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5) {
  int64_t r = __browseros_linux_syscall(nr, a0, a1, a2, a3, a4, a5);
  if (r >= 0 || r < -4095)
    return (long)r;
  unsigned long linux_errno = (unsigned long)-r;
  int value = linux_errno < sizeof errno_of_linux ? errno_of_linux[linux_errno] : 0;
  return -(value ? value : EIO);
}

long __browseros_syscall_ret(long r) {
  if (r < 0 && r >= -4095) {
    errno = (int)-r;
    return -1;
  }
  return r;
}

int __browseros_statx(int dirfd, const char *path, int flags, struct stat *buf) {
  struct __bos_statx stx;
  long r = __bos_syscall5(__BOS_SYS_statx, dirfd, __bos_ptr(path), flags, __BOS_STATX_BASIC_STATS, __bos_ptr(&stx));
  if (r < 0)
    return __browseros_syscall_ret(r);
  // musl の fstatat.c（statx から struct stat を作るところ）と同じ写し方
  *buf = (struct stat){
      .st_dev = ((stx.stx_dev_major & 0xfffff000ULL) << 32) | ((stx.stx_dev_major & 0xfffULL) << 8) |
                ((stx.stx_dev_minor & 0xffffff00ULL) << 12) | (stx.stx_dev_minor & 0xffULL),
      .st_ino = stx.stx_ino,
      .st_mode = stx.stx_mode,
      .st_nlink = stx.stx_nlink,
      .st_uid = stx.stx_uid,
      .st_gid = stx.stx_gid,
      .st_rdev = ((stx.stx_rdev_major & 0xfffff000ULL) << 32) | ((stx.stx_rdev_major & 0xfffULL) << 8) |
                 ((stx.stx_rdev_minor & 0xffffff00ULL) << 12) | (stx.stx_rdev_minor & 0xffULL),
      .st_size = stx.stx_size,
      .st_blksize = stx.stx_blksize,
      .st_blocks = stx.stx_blocks,
      .st_atim = {.tv_sec = stx.stx_atime.tv_sec, .tv_nsec = stx.stx_atime.tv_nsec},
      .st_mtim = {.tv_sec = stx.stx_mtime.tv_sec, .tv_nsec = stx.stx_mtime.tv_nsec},
      .st_ctim = {.tv_sec = stx.stx_ctime.tv_sec, .tv_nsec = stx.stx_ctime.tv_nsec},
  };
  return 0;
}
