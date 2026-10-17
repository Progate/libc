// FS の情報（statfs・fstatfs・statvfs・fstatvfs）と資源の上限（getrlimit・setrlimit・prlimit）。
//
// どれもカーネルの Linux の syscall（statfs64(2)・fstatfs64(2)・prlimit64(2)）を呼ぶだけである（→ browser-os の
// src/wasi/linux/fsinfo.ts・rlimit.ts）。struct statfs と struct rlimit の並びは riscv32 の Linux と同じなので、そのまま渡す。
// statvfs は musl の src/stat/statvfs.c と同じく statfs から写す。

#define _GNU_SOURCE
#include <browseros/libc.h>
#include <browseros/linux.h>
#include <errno.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>

#define SYS_statfs64 43
#define SYS_fstatfs64 44
#define SYS_prlimit64 261

static int ret(long r) { return __browseros_syscall_ret(r); }

int statfs(const char *path, struct statfs *buf) {
  *buf = (struct statfs){0};
  return ret(__bos_syscall3(SYS_statfs64, __bos_ptr(path), sizeof *buf, __bos_ptr(buf)));
}

int fstatfs(int fd, struct statfs *buf) {
  *buf = (struct statfs){0};
  return ret(__bos_syscall3(SYS_fstatfs64, fd, sizeof *buf, __bos_ptr(buf)));
}

/// struct statfs から struct statvfs へ（musl の fixup と同じ）
static void fixup(struct statvfs *out, const struct statfs *in) {
  *out = (struct statvfs){0};
  out->f_bsize = in->f_bsize;
  out->f_frsize = in->f_frsize ? in->f_frsize : in->f_bsize;
  out->f_blocks = in->f_blocks;
  out->f_bfree = in->f_bfree;
  out->f_bavail = in->f_bavail;
  out->f_files = in->f_files;
  out->f_ffree = in->f_ffree;
  out->f_favail = in->f_ffree;
  out->f_fsid = in->f_fsid.__val[0];
  out->f_flag = in->f_flags;
  out->f_namemax = in->f_namelen;
  out->f_type = in->f_type;
}

int statvfs(const char *restrict path, struct statvfs *restrict buf) {
  struct statfs kbuf;
  if (statfs(path, &kbuf) < 0)
    return -1;
  fixup(buf, &kbuf);
  return 0;
}

int fstatvfs(int fd, struct statvfs *buf) {
  struct statfs kbuf;
  if (fstatfs(fd, &kbuf) < 0)
    return -1;
  fixup(buf, &kbuf);
  return 0;
}

int prlimit(pid_t pid, int resource, const struct rlimit *new_limit, struct rlimit *old_limit) {
  return ret(__bos_syscall4(SYS_prlimit64, pid, resource, __bos_ptr(new_limit), __bos_ptr(old_limit)));
}

int getrlimit(int resource, struct rlimit *limit) { return prlimit(0, resource, 0, limit); }

int setrlimit(int resource, const struct rlimit *limit) { return prlimit(0, resource, limit, 0); }
