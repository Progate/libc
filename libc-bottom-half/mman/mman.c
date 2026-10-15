// Userspace emulation of mmap and munmap. Restrictions apply.
//
// This is meant to be complete enough to be compatible with code that uses
// mmap for simple file I/O. It just allocates memory with malloc and reads
// and writes data with pread and pwrite.

#define _WASI_EMULATED_MMAN
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#ifdef __wasilibc_browseros
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#endif

struct map {
  int prot;
  int flags;
  off_t offset;
  size_t length;
#ifdef __wasilibc_browseros
  // MAP_SHARED のファイルの写しは、ほかのプロセスの写しと同期する（→ __browseros_shared_maps_sync）。
  // 呼んだ側が mmap の後で fd を閉じてもよい（POSIX）ので、自分の fd を dup して持つ。同期しないものは -1
  int fd;
  // 最後に同期したときのファイルの中身。写しがこれと違うバイトは自分が書いたもの、ファイルがこれと違うバイトは
  // ほかのプロセスが書いたものである。書けない写しは自分では変わらないので、写しそのものを指す（控えを別に持つと
  // 読むだけの大きなファイルの写しでメモリが 2 倍になる）。同期しないものは NULL
  char *twin;
  // 写しのうち、ファイルの中にある長さ（ファイルの終わりより後ろは同期しない。Linux でもその先は SIGBUS になる）
  size_t synced;
  struct map *next;
#endif
};

#ifdef __wasilibc_browseros
// msync は写しの途中の番地でも呼べる（CPython の mmap.flush(offset, size) はページ境界の番地を渡す）ので、
// 写しを番地から引けるように、生きている写しを列に並べる。スレッド版では複数のスレッドが同時に mmap しうる
static struct map *maps;
static pthread_mutex_t maps_lock = PTHREAD_MUTEX_INITIALIZER;

// ほかのプロセスと同期する写しの数。libc は同期の点（sem_post・waitpid・パイプの read など）でこれを見て、
// 0 なら何もしない（→ libc-bottom-half/sources/browseros/memory_sync.c）
volatile int __browseros_shared_map_count;

// addr から length バイトを含む写しを引く。無ければ NULL。maps_lock を持って呼ぶ
static struct map *find_map_locked(void *addr, size_t length) {
  for (struct map *map = maps; map != NULL; map = map->next) {
    char *body = (char *)(map + 1);
    if ((char *)addr >= body && (char *)addr + length <= body + map->length)
      return map;
  }
  return NULL;
}

// fd の [offset, offset + length) へ書く
static int pwrite_all(int fd, const char *data, size_t length, off_t offset) {
  while (length > 0) {
    ssize_t written = pwrite(fd, data, length, offset);
    if (written < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    data += written;
    offset += written;
    length -= (size_t)written;
  }
  return 0;
}

// fd の [offset, offset + length) を読む。ファイルが縮んでいたら、読めなかったところは 0 にする
static int pread_all(int fd, char *data, size_t length, off_t offset) {
  while (length > 0) {
    ssize_t nread = pread(fd, data, length, offset);
    if (nread < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    if (nread == 0) {
      memset(data, 0, length);
      return 0;
    }
    data += nread;
    offset += nread;
    length -= (size_t)nread;
  }
  return 0;
}

// a と b が違う最初の位置を [from, to) から探す。無ければ to。同じところは 64 バイトずつ memcmp で飛ばす
static size_t next_difference(const char *a, const char *b, size_t from, size_t to) {
  while (from + 64 <= to && memcmp(a + from, b + from, 64) == 0)
    from += 64;
  while (from < to && a[from] == b[from])
    from++;
  return from;
}

// a と b が同じになる最初の位置を [from, to) から探す。無ければ to
static size_t next_agreement(const char *a, const char *b, size_t from, size_t to) {
  while (from < to && a[from] != b[from])
    from++;
  return from;
}

// 写しの [begin, end) のうち、自分が書いたバイト（控えと違うもの）だけをファイルへ書く。書かなかったバイトは、
// ほかのプロセスが書いたものを上書きしない（同じ場所を両方が書いたら、それはプログラムのデータ競合である）
static int publish(struct map *map, size_t begin, size_t end) {
  if (map->twin == NULL || (map->prot & PROT_WRITE) == 0)
    return 0;
  char *body = (char *)(map + 1);
  if (end > map->synced)
    end = map->synced;
  for (size_t at = next_difference(body, map->twin, begin, end); at < end;) {
    size_t until = next_agreement(body, map->twin, at, end);
    if (pwrite_all(map->fd, body + at, until - at, map->offset + (off_t)at) != 0)
      return -1;
    memcpy(map->twin + at, body + at, until - at);
    at = next_difference(body, map->twin, until, end);
  }
  return 0;
}

// ファイルのうち、ほかのプロセスが書いたバイト（控えと違うもの）だけを写しへ取り込む。自分が書いてまだ publish して
// いないバイトは、ファイルの側が変わっていなければそのまま残る（同じプロセスのほかのスレッドが同期の最中に書いても消えない）
static int refresh(struct map *map) {
  if (map->twin == NULL || map->synced == 0)
    return 0;
  char *file = malloc(map->synced);
  if (file == NULL) {
    errno = ENOMEM;
    return -1;
  }
  if (pread_all(map->fd, file, map->synced, map->offset) != 0) {
    int saved_errno = errno;
    free(file);
    errno = saved_errno;
    return -1;
  }
  char *body = (char *)(map + 1);
  for (size_t at = next_difference(file, map->twin, 0, map->synced); at < map->synced;) {
    size_t until = next_agreement(file, map->twin, at, map->synced);
    memcpy(body + at, file + at, until - at);
    if (map->twin != body)
      memcpy(map->twin + at, file + at, until - at);
    at = next_difference(file, map->twin, until, map->synced);
  }
  free(file);
  return 0;
}

// プロセスをまたぐ MAP_SHARED の写しを同期する。
//
// この OS のプロセスはそれぞれ別の線形メモリを持ち、写しを重ねられないので、写しはそのプロセスの malloc の領域にある。
// そこで POSIX がメモリを同期すると定める関数（Base Definitions 4.12 Memory Synchronization の sem_post・sem_wait・
// waitpid など）と、ほかのプロセスと話す口（パイプ・ソケットの read・write）と、プロセスの終わりで、自分が書いたバイトを
// ファイルへ書き、ほかのプロセスが書いたバイトを取り込む。正しく同期しているプログラム（multiprocessing の Lock・
// Queue・join で待ってから読むもの）には、写しを重ねた OS と同じ中身が見える
void __browseros_shared_maps_sync(void) {
  int saved_errno = errno;
  pthread_mutex_lock(&maps_lock);
  for (struct map *map = maps; map != NULL; map = map->next) {
    if (map->twin == NULL)
      continue;
    // 失敗しても同期の点の関数そのものは失敗させない（ファイルが消されたなど。中身は前の同期のときのまま残る）
    if (publish(map, 0, map->synced) == 0)
      refresh(map);
  }
  pthread_mutex_unlock(&maps_lock);
  errno = saved_errno;
}
#endif

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
           off_t offset) {
  // Check for unsupported flags.
  if ((flags & (MAP_PRIVATE | MAP_SHARED)) == 0 || (flags & MAP_FIXED) != 0 ||
#ifdef MAP_SHARED_VALIDATE
      (flags & MAP_SHARED_VALIDATE) == MAP_SHARED_VALIDATE ||
#endif
#ifdef MAP_NORESERVE
      (flags & MAP_NORESERVE) != 0 ||
#endif
#ifdef MAP_GROWSDOWN
      (flags & MAP_GROWSDOWN) != 0 ||
#endif
#ifdef MAP_HUGETLB
      (flags & MAP_HUGETLB) != 0 ||
#endif
#ifdef MAP_FIXED_NOREPLACE
      (flags & MAP_FIXED_NOREPLACE) != 0 ||
#endif
      0) {
    errno = EINVAL;
    return MAP_FAILED;
  }

  // Check for unsupported protection requests.
  if (prot == PROT_NONE ||
#ifdef PROT_EXEC
      (prot & PROT_EXEC) != 0 ||
#endif
      0) {
    errno = EINVAL;
    return MAP_FAILED;
  }

  //  To be consistent with POSIX.
  if (length == 0) {
    errno = EINVAL;
    return MAP_FAILED;
  }

  // Check for integer overflow.
  size_t buf_len = 0;
  if (__builtin_add_overflow(length, sizeof(struct map), &buf_len)) {
    errno = ENOMEM;
    return MAP_FAILED;
  }

  // Allocate the memory.
  struct map *map = malloc(buf_len);
  if (!map) {
    errno = ENOMEM;
    return MAP_FAILED;
  }

  // Initialize the header.
  map->prot = prot;
  map->flags = flags;
  map->offset = offset;
  map->length = length;
#ifdef __wasilibc_browseros
  map->fd = -1;
  map->twin = NULL;
  map->synced = 0;
  if ((flags & MAP_ANON) == 0 && (flags & MAP_SHARED) != 0) {
    struct stat st;
    if (fstat(fd, &st) != 0) {
      int saved_errno = errno;
      free(map);
      errno = saved_errno;
      return MAP_FAILED;
    }
    map->synced = st.st_size > offset ? (size_t)(st.st_size - offset) : 0;
    if (map->synced > length)
      map->synced = length;
    map->fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    map->twin = (prot & PROT_WRITE) == 0 ? (char *)(map + 1) : map->synced > 0 ? malloc(map->synced) : NULL;
    if (map->fd < 0 || (map->synced > 0 && map->twin == NULL)) {
      int saved_errno = map->fd < 0 ? errno : ENOMEM;
      if (map->fd >= 0)
        close(map->fd);
      if (map->twin != (char *)(map + 1))
        free(map->twin);
      free(map);
      errno = saved_errno;
      return MAP_FAILED;
    }
  }
#endif

  // Initialize the main memory buffer, either with the contents of a file,
  // or with zeros.
  addr = map + 1;
  if ((flags & MAP_ANON) == 0) {
    char *body = (char *)addr;
    while (length > 0) {
      const ssize_t nread = pread(fd, body, length, offset);
      if (nread < 0) {
        if (errno == EINTR)
          continue;
        // Free the header allocation; pread already set errno.
        int saved_errno = errno;
#ifdef __wasilibc_browseros
        if (map->fd >= 0)
          close(map->fd);
        if (map->twin != (char *)(map + 1))
          free(map->twin);
#endif
        free(map);
        errno = saved_errno;
        return MAP_FAILED;
      }
      if (nread == 0)
        break;
      length -= (size_t)nread;
      offset += (size_t)nread;
      body += (size_t)nread;
    }
  } else {
    memset(addr, 0, length);
  }

#ifdef __wasilibc_browseros
  if (map->twin != NULL && map->twin != (char *)(map + 1))
    memcpy(map->twin, map + 1, map->synced);
  pthread_mutex_lock(&maps_lock);
  map->next = maps;
  maps = map;
  if (map->twin != NULL)
    __browseros_shared_map_count++;
  pthread_mutex_unlock(&maps_lock);
#endif
  return addr;
}

int munmap(void *addr, size_t length) {
  struct map *map = (struct map *)addr - 1;

  // We don't support partial munmapping.
  if (map->length != length) {
    errno = EINVAL;
    return -1;
  }

#ifdef __wasilibc_browseros
  // 外した写しを列から抜き、自分が書いたバイトをファイルへ書いてから捨てる（Linux でも munmap で書いた中身は
  // 失われない）
  pthread_mutex_lock(&maps_lock);
  for (struct map **link = &maps; *link != NULL; link = &(*link)->next) {
    if (*link == map) {
      *link = map->next;
      break;
    }
  }
  int failed = publish(map, 0, map->synced);
  int saved_errno = errno;
  if (map->twin != NULL)
    __browseros_shared_map_count--;
  pthread_mutex_unlock(&maps_lock);
  if (map->fd >= 0)
    close(map->fd);
  if (map->twin != (char *)(map + 1))
    free(map->twin);
  free(map);
  if (failed) {
    errno = saved_errno;
    return -1;
  }
  return 0;
#else
  // Release the memory.
  free(map);
#endif

  // Success!
  return 0;
}

int mprotect(void *addr, size_t length, int prot) {
  // Address must be page-aligned.
  size_t begin = (size_t)addr;
  if ((begin & (PAGESIZE - 1)) != 0) {
    errno = EINVAL;
    return -1;
  }

  // Length must not be big enough to wrap around.
  size_t end;
  if (__builtin_add_overflow(begin, length, &end)) {
    errno = ENOMEM;
    return -1;
  }

  // Range must be in bounds of linear memory.
  size_t memory_size = __builtin_wasm_memory_size(0) * PAGESIZE;
  if (end > memory_size) {
    errno = ENOMEM;
    return -1;
  }

  // Can only protect memory as read/write (which is a no-op).
  if (prot != (PROT_READ | PROT_WRITE)) {
    errno = ENOTSUP;
    return -1;
  }

  // Success!
  return 0;
}

#ifdef __wasilibc_browseros
// msync(2)。MAP_SHARED のファイルの写しなら、その範囲で自分が書いたバイトをファイルへ書く（MS_ASYNC も、書き戻しを
// 後回しにする理由が無いのでその場で書く）。MS_INVALIDATE なら、ほかのプロセスが書いたバイトも取り込む。
// 写しでない番地は ENOMEM（Linux と同じ）
int msync(void *addr, size_t length, int flags) {
  if (((size_t)addr & (PAGESIZE - 1)) != 0 || (flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0 ||
      (flags & (MS_ASYNC | MS_SYNC)) == (MS_ASYNC | MS_SYNC)) {
    errno = EINVAL;
    return -1;
  }
  if (length == 0)
    return 0;
  pthread_mutex_lock(&maps_lock);
  struct map *map = find_map_locked(addr, length);
  if (map == NULL) {
    pthread_mutex_unlock(&maps_lock);
    errno = ENOMEM;
    return -1;
  }
  size_t begin = (char *)addr - (char *)(map + 1);
  int failed = publish(map, begin, begin + length);
  if (failed == 0 && (flags & MS_INVALIDATE) != 0)
    failed = refresh(map);
  int saved_errno = errno;
  pthread_mutex_unlock(&maps_lock);
  if (failed) {
    errno = saved_errno;
    return -1;
  }
  return 0;
}

// madvise(2) / posix_madvise(3)。助言で、写しは malloc の領域なので従うことが無い。Linux と同じく、
// 知らない助言だけを EINVAL にする
int madvise(void *addr, size_t length, int advice) {
  (void)length;
  if (((size_t)addr & (PAGESIZE - 1)) != 0) {
    errno = EINVAL;
    return -1;
  }
  switch (advice) {
  case MADV_NORMAL:
  case MADV_RANDOM:
  case MADV_SEQUENTIAL:
  case MADV_WILLNEED:
  case MADV_DONTNEED:
  case MADV_FREE:
  case MADV_DONTFORK:
  case MADV_DOFORK:
  case MADV_MERGEABLE:
  case MADV_UNMERGEABLE:
  case MADV_HUGEPAGE:
  case MADV_NOHUGEPAGE:
  case MADV_DONTDUMP:
  case MADV_DODUMP:
    return 0;
  default:
    errno = EINVAL;
    return -1;
  }
}

int posix_madvise(void *addr, size_t length, int advice) {
  (void)addr;
  (void)length;
  switch (advice) {
  case POSIX_MADV_NORMAL:
  case POSIX_MADV_RANDOM:
  case POSIX_MADV_SEQUENTIAL:
  case POSIX_MADV_WILLNEED:
  case POSIX_MADV_DONTNEED:
    return 0;
  default:
    return EINVAL;
  }
}
#endif
