// Linux の syscall の入口を呼ぶ（→ browseros/linux.h）。この libc の errno は Linux の値なので、カーネルが返す -errno をそのまま使う。

#include <browseros/linux.h>
#include <errno.h>
#include <sys/stat.h>

long __browseros_syscall(int nr, int64_t a0, int64_t a1, int64_t a2, int64_t a3, int64_t a4, int64_t a5) {
  return (long)__browseros_linux_syscall(nr, a0, a1, a2, a3, a4, a5);
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
