// fd の読み書きと時計・乱数・終わり方（read・write・pread・pwrite・readv・writev・preadv・pwritev・lseek・close・
// fsync・fdatasync・ftruncate・poll・ppoll・ディレクトリの読み出し・clock_gettime・clock_getres・clock_nanosleep・
// nanosleep・time・gettimeofday・sched_yield・_Exit・getentropy）。
//
// どれもカーネルの Linux の syscall（read(2)・write(2)・_llseek(2)・getdents64(2)・ppoll(2)・clock_gettime(2) ほか）を
// 呼ぶだけである（→ browser-os の src/wasi/linux/io.ts）。読む端の無いパイプへ書いたときの SIGPIPE もカーネルが送る。
//
// この libc のヘッダーの値（POLL*・DT_*・clockid_t）は Linux と同じなので、そのまま渡す。

#define _GNU_SOURCE
#include <browseros/libc.h>
#include <browseros/linux.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/uio.h>
#include <time.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/libc.h>

#define SYS_close 57
#define SYS_getdents64 61
#define SYS__llseek 62
#define SYS_read 63
#define SYS_write 64
#define SYS_readv 65
#define SYS_writev 66
#define SYS_pread64 67
#define SYS_pwrite64 68
#define SYS_preadv 69
#define SYS_pwritev 70
#define SYS_fsync 82
#define SYS_fdatasync 83
#define SYS_exit_group 94
#define SYS_sched_yield 124
#define SYS_getrandom 278
#define SYS_clock_gettime64 403
#define SYS_clock_getres_time64 406
#define SYS_clock_nanosleep_time64 407
#define SYS_ppoll_time64 414
#define SYS_ftruncate64 46

static long ret(long r) { return __browseros_syscall_ret(r); }

ssize_t read(int fd, void *buffer, size_t count) {
  ssize_t got = ret(__bos_syscall3(SYS_read, fd, __bos_ptr(buffer), count));
  if (got > 0)
    __browseros_memory_sync_fd(fd);
  return got;
}

ssize_t write(int fd, const void *buffer, size_t count) {
  __browseros_memory_sync_fd(fd);
  return ret(__bos_syscall3(SYS_write, fd, __bos_ptr(buffer), count));
}

ssize_t pread(int fd, void *buffer, size_t count, off_t offset) {
  return ret(__bos_syscall4(SYS_pread64, fd, __bos_ptr(buffer), count, offset));
}

ssize_t pwrite(int fd, const void *buffer, size_t count, off_t offset) {
  return ret(__bos_syscall4(SYS_pwrite64, fd, __bos_ptr(buffer), count, offset));
}

ssize_t readv(int fd, const struct iovec *iov, int count) {
  ssize_t got = ret(__bos_syscall3(SYS_readv, fd, __bos_ptr(iov), count));
  if (got > 0)
    __browseros_memory_sync_fd(fd);
  return got;
}

ssize_t writev(int fd, const struct iovec *iov, int count) {
  __browseros_memory_sync_fd(fd);
  return ret(__bos_syscall3(SYS_writev, fd, __bos_ptr(iov), count));
}

ssize_t preadv(int fd, const struct iovec *iov, int count, off_t offset) {
  return ret(__bos_syscall4(SYS_preadv, fd, __bos_ptr(iov), count, offset));
}

ssize_t pwritev(int fd, const struct iovec *iov, int count, off_t offset) {
  return ret(__bos_syscall4(SYS_pwritev, fd, __bos_ptr(iov), count, offset));
}

off_t __lseek(int fd, off_t offset, int whence) {
  // riscv32 の Linux の _llseek(2)。位置は上と下の 32 ビットに分けて渡し、新しい位置は result に書かれる。whence の値は Linux と同じ
  int64_t result = 0;
  long r = __bos_syscall5(SYS__llseek, fd, (uint32_t)((uint64_t)offset >> 32), (uint32_t)offset, __bos_ptr(&result),
                          whence);
  return r < 0 ? __browseros_syscall_ret(r) : result;
}

// lseek は unistd.h でマクロにもなっているので、本体は __lseek に置いて別名を張る（wasi-libc と同じ）
__attribute__((__weak__, __alias__("__lseek"))) off_t(lseek)(int, off_t, int);

/// いまの位置（unistd.h の lseek のマクロが、SEEK_CUR で 0 だけ動かす呼び出しをこれにする）
off_t __wasilibc_tell(int fd) { return __lseek(fd, 0, SEEK_CUR); }

int close(int fd) {
  // close(2) は EINTR でも番号を空けている（musl と同じく 0 にする）
  long r = __bos_syscall1(SYS_close, fd);
  if (r == -EINTR)
    r = 0;
  return ret(r);
}

/// fd を番号ごと移す（wasi-libc の口。dup2 して元を閉じる）
int __wasilibc_fd_renumber(int fd, int newfd) {
  if (fd == newfd)
    return fcntl(fd, F_GETFD) < 0 ? -1 : 0;
  if (dup2(fd, newfd) < 0)
    return -1;
  return close(fd);
}

int fsync(int fd) { return ret(__bos_syscall1(SYS_fsync, fd)); }

int fdatasync(int fd) { return ret(__bos_syscall1(SYS_fdatasync, fd)); }

int ftruncate(int fd, off_t length) { return ret(__bos_syscall2(SYS_ftruncate64, fd, length)); }

/// カーネルの struct timespec（64 ビットの秒とナノ秒）
struct kernel_timespec {
  int64_t tv_sec;
  int64_t tv_nsec;
};

int ppoll(struct pollfd *fds, nfds_t count, const struct timespec *timeout, const sigset_t *mask) {
  struct kernel_timespec limit;
  if (timeout) {
    if (timeout->tv_sec < 0 || timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000) {
      errno = EINVAL;
      return -1;
    }
    limit.tv_sec = timeout->tv_sec;
    limit.tv_nsec = timeout->tv_nsec;
  }
  uint64_t raw_mask = 0;
  if (mask)
    memcpy(&raw_mask, mask, sizeof raw_mask);
  return ret(__bos_syscall5(SYS_ppoll_time64, __bos_ptr(fds), count, timeout ? __bos_ptr(&limit) : 0,
                            mask ? __bos_ptr(&raw_mask) : 0, 8));
}

int poll(struct pollfd *fds, nfds_t count, int timeout) {
  if (timeout < 0)
    return ppoll(fds, count, 0, 0);
  struct timespec limit = {.tv_sec = timeout / 1000, .tv_nsec = (long)(timeout % 1000) * 1000000};
  return ppoll(fds, count, &limit, 0);
}

int __clock_gettime(clockid_t clock, struct timespec *tp) {
  struct kernel_timespec raw;
  long r = __bos_syscall2(SYS_clock_gettime64, clock, __bos_ptr(&raw));
  if (r < 0)
    return ret(r);
  tp->tv_sec = raw.tv_sec;
  tp->tv_nsec = raw.tv_nsec;
  return 0;
}

__attribute__((__weak__, __alias__("__clock_gettime"))) int clock_gettime(clockid_t clock, struct timespec *tp);

int clock_getres(clockid_t clock, struct timespec *resolution) {
  struct kernel_timespec raw;
  long r = __bos_syscall2(SYS_clock_getres_time64, clock, __bos_ptr(&raw));
  if (r < 0)
    return ret(r);
  if (resolution) {
    resolution->tv_sec = raw.tv_sec;
    resolution->tv_nsec = raw.tv_nsec;
  }
  return 0;
}

/// clock_nanosleep(3) はエラーを errno ではなく戻り値で返す
int clock_nanosleep(clockid_t clock, int flags, const struct timespec *request, struct timespec *remain) {
  struct kernel_timespec raw_request = {request->tv_sec, request->tv_nsec}, raw_remain = {0, 0};
  long r = __bos_syscall4(SYS_clock_nanosleep_time64, clock, flags & TIMER_ABSTIME ? 1 : 0,
                          __bos_ptr(&raw_request), remain ? __bos_ptr(&raw_remain) : 0);
  if (r == -EINTR && remain) {
    remain->tv_sec = raw_remain.tv_sec;
    remain->tv_nsec = raw_remain.tv_nsec;
  }
  return r < 0 ? (int)-r : 0;
}

__attribute__((__weak__, __alias__("clock_nanosleep"))) int __clock_nanosleep(clockid_t, int, const struct timespec *,
                                                                              struct timespec *);

int nanosleep(const struct timespec *request, struct timespec *remain) {
  int error = clock_nanosleep(CLOCK_MONOTONIC, 0, request, remain);
  if (error) {
    errno = error;
    return -1;
  }
  return 0;
}

time_t time(time_t *out) {
  struct timespec now;
  __clock_gettime(CLOCK_REALTIME, &now);
  if (out)
    *out = now.tv_sec;
  return now.tv_sec;
}

int gettimeofday(struct timeval *restrict tv, void *restrict tz) {
  (void)tz;
  if (tv) {
    struct timespec now;
    __clock_gettime(CLOCK_REALTIME, &now);
    tv->tv_sec = now.tv_sec;
    tv->tv_usec = now.tv_nsec / 1000;
  }
  return 0;
}

int sched_yield(void) { return ret(__bos_syscall0(SYS_sched_yield)); }

_Noreturn void _Exit(int status) {
  __bos_syscall1(SYS_exit_group, status);
  __builtin_unreachable();
}

int getentropy(void *buffer, size_t length) {
  if (length > 256) {
    errno = EIO;
    return -1;
  }
  long r = __bos_syscall3(SYS_getrandom, __bos_ptr(buffer), length, 0);
  return r < 0 ? ret(r) : 0;
}

// ディレクトリの読み出し（opendir は paths.c）。カーネルの getdents64(2) で struct linux_dirent64 の並びを受け取り、
// この libc の struct dirent（d_ino・d_type・d_name）に写す（d_type は Linux と同じ値）。位置は読んだ項目の数で、telldir・seekdir はそれを使う
// （musl の src/dirent と同じ作り）

struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

struct _DIR {
  int fd;
  long tell;
  int buf_pos;
  int buf_end;
  char buf[2048];
  struct dirent *dirent;
};

DIR *fdopendir(int fd) {
  struct stat st;
  if (fstat(fd, &st) < 0)
    return 0;
  if (!S_ISDIR(st.st_mode)) {
    errno = ENOTDIR;
    return 0;
  }
  DIR *dir = calloc(1, sizeof *dir);
  if (!dir)
    return 0;
  dir->dirent = malloc(sizeof(struct dirent) + NAME_MAX + 1);
  if (!dir->dirent) {
    free(dir);
    return 0;
  }
  dir->fd = fd;
  fcntl(fd, F_SETFD, FD_CLOEXEC);
  return dir;
}

struct dirent *readdir(DIR *dir) {
  if (dir->buf_pos >= dir->buf_end) {
    long length = __bos_syscall3(SYS_getdents64, dir->fd, __bos_ptr(dir->buf), sizeof dir->buf);
    if (length <= 0) {
      if (length < 0 && length != -ENOENT)
        errno = -length;
      return 0;
    }
    dir->buf_end = length;
    dir->buf_pos = 0;
  }
  struct linux_dirent64 *raw = (void *)(dir->buf + dir->buf_pos);
  dir->buf_pos += raw->d_reclen;
  dir->tell = raw->d_off;
  dir->dirent->d_ino = raw->d_ino;
  dir->dirent->d_type = raw->d_type;
  strncpy(dir->dirent->d_name, raw->d_name, NAME_MAX);
  dir->dirent->d_name[NAME_MAX] = '\0';
  return dir->dirent;
}

int readdir_r(DIR *restrict dir, struct dirent *restrict entry, struct dirent **restrict result) {
  int saved = errno;
  errno = 0;
  struct dirent *found = readdir(dir);
  int error = errno;
  errno = saved;
  if (error)
    return error;
  if (found)
    memcpy(entry, found, sizeof *found + strlen(found->d_name) + 1);
  *result = found ? entry : 0;
  return 0;
}

void seekdir(DIR *dir, long offset) {
  lseek(dir->fd, offset, SEEK_SET);
  dir->buf_pos = dir->buf_end = 0;
  dir->tell = offset;
}

void rewinddir(DIR *dir) { seekdir(dir, 0); }

long telldir(DIR *dir) { return dir->tell; }

int dirfd(DIR *dir) { return dir->fd; }

/// ディレクトリの流れを閉じ、fd は残して返す（wasi-libc の口）
int fdclosedir(DIR *dir) {
  int fd = dir->fd;
  free(dir->dirent);
  free(dir);
  return fd;
}

int closedir(DIR *dir) { return close(fdclosedir(dir)); }

/// scandirat(3) の中身（→ paths.c）。読み出した項目を filter で選び、compar で並べる
int __wasilibc_nocwd_scandirat(int fd, const char *path, struct dirent ***namelist, int (*filter)(const struct dirent *),
                               int (*compar)(const struct dirent **, const struct dirent **)) {
  DIR *dir = opendirat(fd, path);
  if (!dir)
    return -1;
  struct dirent **list = 0;
  size_t count = 0, capacity = 0;
  for (struct dirent *entry; (entry = readdir(dir));) {
    if (filter && !filter(entry))
      continue;
    if (count == capacity) {
      capacity = capacity ? capacity * 2 : 16;
      struct dirent **grown = realloc(list, capacity * sizeof *list);
      if (!grown)
        goto failed;
      list = grown;
    }
    size_t size = sizeof *entry + strlen(entry->d_name) + 1;
    list[count] = malloc(size);
    if (!list[count])
      goto failed;
    memcpy(list[count++], entry, size);
  }
  closedir(dir);
  if (compar)
    qsort(list, count, sizeof *list, (int (*)(const void *, const void *))compar);
  *namelist = list;
  return (int)count;
failed:
  while (count)
    free(list[--count]);
  free(list);
  closedir(dir);
  errno = ENOMEM;
  return -1;
}
