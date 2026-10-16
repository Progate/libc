// Copyright (c) 2015-2016 Nuxi, https://nuxi.nl/
//
// SPDX-License-Identifier: BSD-2-Clause

#include "stat_impl.h"
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wasi/api.h>
#include <wasi/descriptor_table.h>

#ifdef __wasip1__
#include <browseros/linux.h>
#else
#include <stddefer.h>
#endif

int fstat(int fildes, struct stat *buf) {
#if defined(__wasip1__)
  // BrowserOS: カーネルの statx(2) で fd そのもの（AT_EMPTY_PATH）を引く。種類と権限のビット（パイプは S_IFIFO、
  // 端末は S_IFCHR）・持ち主・時刻はカーネルが Linux の struct statx で答える
  return __browseros_statx(fildes, "", __BOS_AT_EMPTY_PATH, buf);
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
