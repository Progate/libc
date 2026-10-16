// 作業ディレクトリとパスの関数（open・openat・access・faccessat・readlink・symlink・link・mkdir・rmdir・unlink・
// rename・chdir・fchdir・getcwd・stat・lstat・fstatat・utimensat・utime・utimes・chmod・fchmodat・chown・lchown・
// fchownat・fchown・truncate・opendir・scandir・remove）。
//
// 作業ディレクトリはカーネルが持つ（Linux と同じ。→ browser-os の src/wasi/linux/paths.ts）。wasi-libc は作業ディレクトリ
// を自分で持ち、パスを preopen とそこからの相対パスに直してから WASI の path_* を呼ぶが、この libc はパスをそのまま
// カーネルの *at(2) に渡す。`AT_FDCWD` からの相対パスはカーネルが作業ディレクトリから引き、新しく作るものの権限
// （mode & ~umask）もカーネルが付ける。
//
// この libc のヘッダーの値（O_*・AT_*・UTIME_*）は Linux と同じなので、フラグはそのまま渡す。

#define _GNU_SOURCE
#include <browseros/linux.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/time.h>
#include <unistd.h>
#include <utime.h>
#include <wasi/libc-nocwd.h>
#include <wasi/libc.h>

static int ret(long r) { return __browseros_syscall_ret(r); }

int openat(int fd, const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, mode_t);
    va_end(ap);
  }
  return ret(__bos_syscall4(__BOS_SYS_openat, fd, __bos_ptr(path), flags, mode));
}

int open(const char *path, int flags, ...) {
  mode_t mode = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    mode = va_arg(ap, mode_t);
    va_end(ap);
  }
  return openat(AT_FDCWD, path, flags, mode);
}

int __wasilibc_open_nomode(const char *path, int flags) { return openat(AT_FDCWD, path, flags, 0); }

int faccessat(int fd, const char *path, int mode, int flags) {
  if (flags)
    return ret(__bos_syscall4(__BOS_SYS_faccessat2, fd, __bos_ptr(path), mode, flags));
  return ret(__bos_syscall3(__BOS_SYS_faccessat, fd, __bos_ptr(path), mode));
}

int access(const char *path, int mode) { return faccessat(AT_FDCWD, path, mode, 0); }

int __wasilibc_access(const char *path, int mode, int flags) { return faccessat(AT_FDCWD, path, mode, flags); }

ssize_t readlinkat(int fd, const char *restrict path, char *restrict buf, size_t size) {
  return ret(__bos_syscall4(__BOS_SYS_readlinkat, fd, __bos_ptr(path), __bos_ptr(buf), size));
}

ssize_t readlink(const char *restrict path, char *restrict buf, size_t size) {
  return readlinkat(AT_FDCWD, path, buf, size);
}

int symlinkat(const char *target, int fd, const char *path) {
  return ret(__bos_syscall3(__BOS_SYS_symlinkat, __bos_ptr(target), fd, __bos_ptr(path)));
}

int symlink(const char *target, const char *path) { return symlinkat(target, AT_FDCWD, path); }

int linkat(int oldfd, const char *oldpath, int newfd, const char *newpath, int flags) {
  return ret(__bos_syscall5(__BOS_SYS_linkat, oldfd, __bos_ptr(oldpath), newfd, __bos_ptr(newpath), flags));
}

int link(const char *oldpath, const char *newpath) { return linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, 0); }

int __wasilibc_link(const char *oldpath, const char *newpath, int flags) {
  return linkat(AT_FDCWD, oldpath, AT_FDCWD, newpath, flags);
}

int __wasilibc_link_oldat(int oldfd, const char *oldpath, const char *newpath, int flags) {
  return linkat(oldfd, oldpath, AT_FDCWD, newpath, flags);
}

int __wasilibc_link_newat(const char *oldpath, int newfd, const char *newpath, int flags) {
  return linkat(AT_FDCWD, oldpath, newfd, newpath, flags);
}

int mkdirat(int fd, const char *path, mode_t mode) {
  return ret(__bos_syscall3(__BOS_SYS_mkdirat, fd, __bos_ptr(path), mode));
}

int mkdir(const char *path, mode_t mode) { return mkdirat(AT_FDCWD, path, mode); }

int unlinkat(int fd, const char *path, int flags) {
  return ret(__bos_syscall3(__BOS_SYS_unlinkat, fd, __bos_ptr(path), flags));
}

int unlink(const char *path) { return unlinkat(AT_FDCWD, path, 0); }

int rmdir(const char *path) { return unlinkat(AT_FDCWD, path, AT_REMOVEDIR); }

int __wasilibc_unlinkat(int fd, const char *path) { return unlinkat(fd, path, 0); }

int __wasilibc_rmdirat(int fd, const char *path) { return unlinkat(fd, path, AT_REMOVEDIR); }

/// remove(3)。ファイルとして消し、ディレクトリなら rmdir する（musl と同じ）
int remove(const char *path) {
  int r = unlink(path);
  if (r < 0 && errno == EISDIR)
    r = rmdir(path);
  return r;
}

int renameat(int oldfd, const char *oldpath, int newfd, const char *newpath) {
  return ret(__bos_syscall5(__BOS_SYS_renameat2, oldfd, __bos_ptr(oldpath), newfd, __bos_ptr(newpath), 0));
}

int rename(const char *oldpath, const char *newpath) { return renameat(AT_FDCWD, oldpath, AT_FDCWD, newpath); }

int __wasilibc_rename_oldat(int oldfd, const char *oldpath, const char *newpath) {
  return renameat(oldfd, oldpath, AT_FDCWD, newpath);
}

int __wasilibc_rename_newat(const char *oldpath, int newfd, const char *newpath) {
  return renameat(AT_FDCWD, oldpath, newfd, newpath);
}

int chdir(const char *path) { return ret(__bos_syscall1(__BOS_SYS_chdir, __bos_ptr(path))); }

int fchdir(int fd) { return ret(__bos_syscall1(__BOS_SYS_fchdir, fd)); }

/// getcwd(3)。`buf` が NULL なら大きさの合う領域を作って返す（musl・glibc と同じ）
char *getcwd(char *buf, size_t size) {
  char scratch[PATH_MAX];
  if (!buf) {
    buf = scratch;
    size = sizeof scratch;
  } else if (!size) {
    errno = EINVAL;
    return 0;
  }
  long r = __bos_syscall2(__BOS_SYS_getcwd, __bos_ptr(buf), size);
  if (r < 0) {
    errno = -r;
    return 0;
  }
  return buf == scratch ? strdup(scratch) : buf;
}

int fstatat(int fd, const char *restrict path, struct stat *restrict buf, int flags) {
  return __browseros_statx(fd, path, flags, buf);
}

int stat(const char *restrict path, struct stat *restrict buf) { return fstatat(AT_FDCWD, path, buf, 0); }

int lstat(const char *restrict path, struct stat *restrict buf) {
  return fstatat(AT_FDCWD, path, buf, AT_SYMLINK_NOFOLLOW);
}

int __wasilibc_stat(const char *restrict path, struct stat *restrict buf, int flags) {
  return fstatat(AT_FDCWD, path, buf, flags);
}

/// カーネルの struct timespec（64 ビットの秒とナノ秒）
struct kernel_timespec {
  int64_t tv_sec;
  int64_t tv_nsec;
};

/// utimensat(2)。`path` が NULL なら `fd` そのもの（futimens）
static int kernel_utimensat(int fd, const char *path, const struct timespec times[2], int flags) {
  struct kernel_timespec raw[2];
  if (times) {
    for (int i = 0; i < 2; i++) {
      raw[i].tv_sec = times[i].tv_sec;
      raw[i].tv_nsec = times[i].tv_nsec;
    }
  }
  return ret(__bos_syscall4(__BOS_SYS_utimensat_time64, fd, __bos_ptr(path), times ? __bos_ptr(raw) : 0, flags));
}

int utimensat(int fd, const char *path, const struct timespec times[2], int flags) {
  if (!path) {
    errno = EINVAL;
    return -1;
  }
  return kernel_utimensat(fd, path, times, flags);
}

int futimens(int fd, const struct timespec times[2]) { return kernel_utimensat(fd, 0, times, 0); }

int __wasilibc_utimens(const char *path, const struct timespec times[2], int flags) {
  return utimensat(AT_FDCWD, path, times, flags);
}

int utime(const char *path, const struct utimbuf *times) {
  if (!times)
    return utimensat(AT_FDCWD, path, 0, 0);
  struct timespec ts[2] = {{.tv_sec = times->actime}, {.tv_sec = times->modtime}};
  return utimensat(AT_FDCWD, path, ts, 0);
}

int utimes(const char *path, const struct timeval times[2]) {
  if (!times)
    return utimensat(AT_FDCWD, path, 0, 0);
  struct timespec ts[2] = {{.tv_sec = times[0].tv_sec, .tv_nsec = times[0].tv_usec * 1000},
                           {.tv_sec = times[1].tv_sec, .tv_nsec = times[1].tv_usec * 1000}};
  return utimensat(AT_FDCWD, path, ts, 0);
}

int fchmodat(int fd, const char *path, mode_t mode, int flags) {
  // Linux の fchmodat(2) は flags を受けない（AT_SYMLINK_NOFOLLOW はリンクの権限を変えられないので ENOTSUP。musl と同じ）
  if (flags & AT_SYMLINK_NOFOLLOW) {
    errno = ENOTSUP;
    return -1;
  }
  return ret(__bos_syscall3(__BOS_SYS_fchmodat, fd, __bos_ptr(path), mode));
}

int chmod(const char *path, mode_t mode) { return fchmodat(AT_FDCWD, path, mode, 0); }

int fchmod(int fd, mode_t mode) { return ret(__bos_syscall2(__BOS_SYS_fchmod, fd, mode)); }

int fchownat(int fd, const char *path, uid_t owner, gid_t group, int flags) {
  return ret(__bos_syscall5(__BOS_SYS_fchownat, fd, __bos_ptr(path), owner, group, flags));
}

int chown(const char *path, uid_t owner, gid_t group) { return fchownat(AT_FDCWD, path, owner, group, 0); }

int lchown(const char *path, uid_t owner, gid_t group) {
  return fchownat(AT_FDCWD, path, owner, group, AT_SYMLINK_NOFOLLOW);
}

int fchown(int fd, uid_t owner, gid_t group) { return ret(__bos_syscall3(__BOS_SYS_fchown, fd, owner, group)); }

int truncate(const char *path, off_t length) {
  return ret(__bos_syscall2(__BOS_SYS_truncate64, __bos_ptr(path), length));
}

/// opendir(3)。ディレクトリを O_DIRECTORY で開き、fdopendir に渡す（musl と同じ）
DIR *opendirat(int fd, const char *path) {
  int dirfd = openat(fd, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dirfd < 0)
    return 0;
  DIR *dir = fdopendir(dirfd);
  if (!dir)
    close(dirfd);
  return dir;
}

DIR *opendir(const char *path) { return opendirat(AT_FDCWD, path); }

int scandirat(int fd, const char *path, struct dirent ***namelist, int (*filter)(const struct dirent *),
              int (*compar)(const struct dirent **, const struct dirent **)) {
  int dirfd = openat(fd, path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (dirfd < 0)
    return -1;
  int r = __wasilibc_nocwd_scandirat(dirfd, ".", namelist, filter, compar);
  int saved = errno;
  close(dirfd);
  errno = saved;
  return r;
}

int scandir(const char *path, struct dirent ***namelist, int (*filter)(const struct dirent *),
            int (*compar)(const struct dirent **, const struct dirent **)) {
  return scandirat(AT_FDCWD, path, namelist, filter, compar);
}

/// statvfs(3)。カーネルはまだ statfs(2) を持たない（ENOSYS。wasi-libc と同じ）
int statvfs(const char *restrict path, struct statvfs *restrict buf) {
  (void)path;
  (void)buf;
  errno = ENOSYS;
  return -1;
}

int fstatvfs(int fd, struct statvfs *buf) {
  (void)fd;
  (void)buf;
  errno = ENOSYS;
  return -1;
}

/// posix_fadvise(3)。先読みの助言は受け取るだけで、効き目は無い（Linux と同じく、開いていなければ EBADF、パイプなら
/// ESPIPE、知らない助言なら EINVAL）。失敗は errno ではなく戻り値で返す
int posix_fadvise(int fd, off_t offset, off_t length, int advice) {
  (void)offset;
  struct stat st;
  if (fstat(fd, &st) != 0)
    return errno;
  if (S_ISFIFO(st.st_mode))
    return ESPIPE;
  if (advice < POSIX_FADV_NORMAL || advice > POSIX_FADV_NOREUSE || length < 0)
    return EINVAL;
  return 0;
}

/// posix_fallocate(3)。足りなければ ftruncate で伸ばす（FS は場所を前もって取らないので、大きさだけが変わる）。
/// 失敗は errno ではなく戻り値で返す
int posix_fallocate(int fd, off_t offset, off_t length) {
  if (offset < 0 || length <= 0)
    return EINVAL;
  struct stat st;
  if (fstat(fd, &st) != 0)
    return errno;
  if (S_ISFIFO(st.st_mode))
    return ESPIPE;
  if (!S_ISREG(st.st_mode))
    return ENODEV;
  if (offset + length > st.st_size && ftruncate(fd, offset + length) != 0)
    return errno == EINVAL ? EBADF : errno;
  return 0;
}

// wasi-libc の「作業ディレクトリを使わない」版（<wasi/libc-nocwd.h>）。この libc の *at はカーネルへそのまま渡すので、
// 同じ関数を呼ぶ
int __wasilibc_nocwd_openat_nomode(int fd, const char *path, int flags) { return openat(fd, path, flags, 0); }

int __wasilibc_nocwd_faccessat(int fd, const char *path, int mode, int flags) {
  return faccessat(fd, path, mode, flags);
}

ssize_t __wasilibc_nocwd_readlinkat(int fd, const char *restrict path, char *restrict buf, size_t size) {
  return readlinkat(fd, path, buf, size);
}

int __wasilibc_nocwd_symlinkat(const char *target, int fd, const char *path) { return symlinkat(target, fd, path); }

int __wasilibc_nocwd_linkat(int oldfd, const char *oldpath, int newfd, const char *newpath, int flags) {
  return linkat(oldfd, oldpath, newfd, newpath, flags);
}

int __wasilibc_nocwd_renameat(int oldfd, const char *oldpath, int newfd, const char *newpath) {
  return renameat(oldfd, oldpath, newfd, newpath);
}

int __wasilibc_nocwd_mkdirat_nomode(int fd, const char *path) { return mkdirat(fd, path, 0777); }

int __wasilibc_nocwd_utimensat(int fd, const char *path, const struct timespec times[2], int flags) {
  return utimensat(fd, path, times, flags);
}

int __wasilibc_nocwd___wasilibc_unlinkat(int fd, const char *path) { return unlinkat(fd, path, 0); }

int __wasilibc_nocwd___wasilibc_rmdirat(int fd, const char *path) { return unlinkat(fd, path, AT_REMOVEDIR); }
