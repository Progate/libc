// POSIX の名前つきセマフォ（sem_open・sem_close・sem_unlink）と、その sem_t への sem_wait などの操作。
//
// 値と待ち手はカーネルの表にあり（browser_os_sem。→ browser-os の src/process/semaphores.ts）、どのプロセスが
// 同じ名前を開いても同じセマフォを見る。sem_t には名前つきの印とカーネルの handle だけを置く（→ browseros_sem.h）。
//
// 同じプロセスが同じセマフォを開き直したら、同じ sem_t * を返し、sem_close の数を数える（sem_open(3)）。
// 「同じセマフォ」はカーネルが返す id で見分ける。名前で見分けると、sem_unlink のあとに同じ名前で作り直した
// 別のセマフォに古い sem_t を返してしまう。
//
// sem_post の前と、取れた sem_wait の後で、プロセスをまたぐ MAP_SHARED の写しを同期する（→ memory_sync.c）。
// multiprocessing の Lock で守った共有メモリ（Value・Array）は、これで取った側から見える。
//
// 名前の形は musl の __shm_mapname と同じく確かめる（先頭の `/` を外し、途中に `/` を含むもの・空のもの・
// `.`・`..` は EINVAL、長すぎるものは ENAMETOOLONG）。権限（mode）はカーネルの表に無いので使わない。

#define _GNU_SOURCE
#include <browseros/host.h>
#include <browseros/libc.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "browseros_sem.h"

/// このプロセスが開いている名前つきセマフォ（同じものを開き直したら同じ sem_t * を返すための表）
struct named {
  uint32_t id;
  sem_t *sem;
  int refcnt;
};

static struct named *opened;
static size_t opened_len;
static size_t opened_cap;
static pthread_mutex_t opened_lock = PTHREAD_MUTEX_INITIALIZER;

/// 名前を確かめ、先頭の `/` を外した名前とその長さを返す。正しくなければ errno を置いて NULL
static const char *name_of(const char *name, size_t *len) {
  while (*name == '/')
    name++;
  const char *end = strchrnul(name, '/');
  if (*end || end == name || (end - name <= 2 && name[0] == '.' && end[-1] == '.')) {
    errno = EINVAL;
    return 0;
  }
  // Linux は `sem.` を付けた名前を /dev/shm に置くので、名前は NAME_MAX から 4 文字少ない長さまで
  if (end - name > NAME_MAX - 4) {
    errno = ENAMETOOLONG;
    return 0;
  }
  *len = end - name;
  return name;
}

sem_t *sem_open(const char *name, int flags, ...) {
  unsigned value = 0;
  if (flags & O_CREAT) {
    va_list ap;
    va_start(ap, flags);
    va_arg(ap, mode_t);
    value = va_arg(ap, unsigned);
    va_end(ap);
    if (value > SEM_VALUE_MAX) {
      errno = EINVAL;
      return SEM_FAILED;
    }
  }
  size_t len;
  if (!(name = name_of(name, &len)))
    return SEM_FAILED;
  uint32_t handle = 0, id = 0;
  // カーネルの口は O_CREAT・O_EXCL を自分のビット（1 と 2）で受け取る（libc の O_* の値に寄らない）
  int32_t kernel_flags = ((flags & O_CREAT) ? 1 : 0) | ((flags & O_CREAT) && (flags & O_EXCL) ? 2 : 0);
  int32_t error = __browseros_ksem_open(name, len, kernel_flags, value, &handle, &id);
  if (error != 0) {
    errno = error;
    return SEM_FAILED;
  }

  pthread_mutex_lock(&opened_lock);
  for (size_t i = 0; i < opened_len; i++) {
    if (opened[i].id == id) {
      opened[i].refcnt++;
      sem_t *sem = opened[i].sem;
      pthread_mutex_unlock(&opened_lock);
      // 開き直した handle は要らない（このプロセスはすでに 1 つ持っている）
      __browseros_ksem_close(handle);
      return sem;
    }
  }
  sem_t *sem = malloc(sizeof *sem);
  if (sem && opened_len == opened_cap) {
    size_t cap = opened_cap ? opened_cap * 2 : 8;
    struct named *grown = realloc(opened, cap * sizeof *grown);
    if (grown) {
      opened = grown;
      opened_cap = cap;
    } else {
      free(sem);
      sem = 0;
    }
  }
  if (!sem) {
    pthread_mutex_unlock(&opened_lock);
    __browseros_ksem_close(handle);
    errno = ENOMEM;
    return SEM_FAILED;
  }
  memset(sem, 0, sizeof *sem);
  sem->__val[2] = __BROWSEROS_SEM_NAMED;
  sem->__val[3] = (int)handle;
  opened[opened_len++] = (struct named){.id = id, .sem = sem, .refcnt = 1};
  pthread_mutex_unlock(&opened_lock);
  return sem;
}

int sem_close(sem_t *sem) {
  pthread_mutex_lock(&opened_lock);
  for (size_t i = 0; i < opened_len; i++) {
    if (opened[i].sem != sem)
      continue;
    if (--opened[i].refcnt > 0) {
      pthread_mutex_unlock(&opened_lock);
      return 0;
    }
    opened[i] = opened[--opened_len];
    pthread_mutex_unlock(&opened_lock);
    __browseros_ksem_close((uint32_t)sem->__val[3]);
    free(sem);
    return 0;
  }
  pthread_mutex_unlock(&opened_lock);
  errno = EINVAL;
  return -1;
}

int sem_unlink(const char *name) {
  size_t len;
  if (!(name = name_of(name, &len)))
    return -1;
  int32_t error = __browseros_ksem_unlink(name, len);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
}

int __browseros_sem_timedwait(sem_t *sem, const struct timespec *at) {
  pthread_testcancel();
  int64_t deadline = -1;
  if (at) {
    if (at->tv_nsec < 0 || at->tv_nsec >= 1000000000L) {
      errno = EINVAL;
      return -1;
    }
    // 過ぎた時刻は 0 にする（カーネルは負の値を「期限なし」と読む）
    deadline = at->tv_sec < 0 ? 0 : (int64_t)at->tv_sec * 1000000000LL + at->tv_nsec;
  }
  int32_t error = __browseros_ksem_timedwait((uint32_t)sem->__val[3], deadline);
  if (error != 0) {
    errno = error;
    return -1;
  }
  __browseros_memory_sync();
  return 0;
}

int __browseros_sem_trywait(sem_t *sem) {
  int32_t error = __browseros_ksem_trywait((uint32_t)sem->__val[3]);
  if (error != 0) {
    errno = error;
    return -1;
  }
  __browseros_memory_sync();
  return 0;
}

int __browseros_sem_post(sem_t *sem) {
  __browseros_memory_sync();
  int32_t error = __browseros_ksem_post((uint32_t)sem->__val[3]);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
}

int __browseros_sem_getvalue(sem_t *sem, int *valp) {
  int32_t error = __browseros_ksem_getvalue((uint32_t)sem->__val[3], valp);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
}
