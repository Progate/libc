#ifndef __wasilibc___struct_stat_h
#define __wasilibc___struct_stat_h

#include <__struct_timespec.h>
#include <__typedef_blkcnt_t.h>
#include <__typedef_blksize_t.h>
#include <__typedef_dev_t.h>
#include <__typedef_gid_t.h>
#include <__typedef_ino_t.h>
#include <__typedef_mode_t.h>
#include <__typedef_nlink_t.h>
#include <__typedef_off_t.h>
#include <__typedef_uid_t.h>

/* BrowserOS: riscv32 の Linux（musl の arch/generic/bits/stat.h）と同じ並び */
struct stat {
  dev_t st_dev;
  ino_t st_ino;
  mode_t st_mode;
  nlink_t st_nlink;
  uid_t st_uid;
  gid_t st_gid;
  dev_t st_rdev;
  unsigned long __pad;
  off_t st_size;
  blksize_t st_blksize;
  int __pad2;
  blkcnt_t st_blocks;

  struct timespec st_atim;
  struct timespec st_mtim;
  struct timespec st_ctim;
  unsigned __unused[2];
};

#endif
