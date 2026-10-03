// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include "stat_impl.h"
#include <errno.h>
#include <sys/stat.h>
#include <wasi/api.h>
#include <wasi/descriptor_table.h>

#ifdef __wasip1__
#include <browseros/host.h>
#else
#include <stddefer.h>
#endif

int fstat(int fildes, struct stat *buf) {
#if defined(__wasip1__)
  // BrowserOS: WASI の filestat には権限が無いので、filestat と種類・権限のビット（パイプは S_IFIFO、端末は S_IFCHR）を
  // カーネルに 1 度で聞く。preview1 の fd_filestat_get と別々に聞くと FS への問い合わせが 2 往復になり、stat が倍遅くなる。
  // カーネルが答えない fd（ENOTSUP）と BrowserOS でないホスト（ENOSYS）では、preview1 で聞いて種類のビットだけにする
  __wasi_filestat_t internal_stat;
  uint32_t mode;
  int32_t error = __browseros_fstat(fildes, &internal_stat, &mode);
  int has_mode = error == 0;
  if (error == __WASI_ERRNO_NOTSUP || error == __WASI_ERRNO_NOSYS)
    error = __wasi_fd_filestat_get(fildes, &internal_stat);
  if (error != 0) {
    errno = error;
    return -1;
  }
  to_public_stat(&internal_stat, buf);
  if (has_mode)
    buf->st_mode = mode;
  return 0;
#elif defined(__wasip2__) || defined(__wasip3__)
  // Translate the file descriptor to an internal handle
  descriptor_table_entry_t entry;
  if (descriptor_table_get(fildes, &entry) < 0)
    return -1;
  defer descriptor_table_entry_dec(entry);
  return entry.vtable->fstat(entry.data, buf);
#else
# error "Unsupported WASI version"
#endif
}
