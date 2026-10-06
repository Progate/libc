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
#endif

struct map {
  int prot;
  int flags;
  off_t offset;
  size_t length;
#ifdef __wasilibc_browseros
  // MAP_SHARED で書けるファイルの写しは、msync と munmap でファイルへ書き戻す。呼んだ側が mmap の後で fd を
  // 閉じてもよい（POSIX）ので、自分の fd を dup して持つ。書き戻さないものは -1
  int fd;
  struct map *next;
#endif
};

#ifdef __wasilibc_browseros
// msync は写しの途中の番地でも呼べる（CPython の mmap.flush(offset, size) はページ境界の番地を渡す）ので、
// 写しを番地から引けるように、生きている写しを列に並べる。スレッド版では複数のスレッドが同時に mmap しうる
static struct map *maps;
static pthread_mutex_t maps_lock = PTHREAD_MUTEX_INITIALIZER;

// addr から length バイトを含む写しを引く。無ければ NULL
static struct map *find_map(void *addr, size_t length) {
  struct map *found = NULL;
  pthread_mutex_lock(&maps_lock);
  for (struct map *map = maps; map != NULL; map = map->next) {
    char *body = (char *)(map + 1);
    if ((char *)addr >= body && (char *)addr + length <= body + map->length) {
      found = map;
      break;
    }
  }
  pthread_mutex_unlock(&maps_lock);
  return found;
}

// 写しの [begin, begin + length) をファイルの同じ位置へ書き戻す
static int write_back(struct map *map, char *begin, size_t length) {
  if (map->fd < 0)
    return 0;
  off_t offset = map->offset + (begin - (char *)(map + 1));
  while (length > 0) {
    ssize_t written = pwrite(map->fd, begin, length, offset);
    if (written < 0) {
      if (errno == EINTR)
        continue;
      return -1;
    }
    begin += written;
    offset += written;
    length -= (size_t)written;
  }
  return 0;
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
  if ((flags & MAP_ANON) == 0 && (flags & MAP_SHARED) != 0 && (prot & PROT_WRITE) != 0) {
    map->fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
    if (map->fd < 0) {
      int saved_errno = errno;
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
  pthread_mutex_lock(&maps_lock);
  map->next = maps;
  maps = map;
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
  // 外した写しを列から抜き、書けるファイルの写しはファイルへ書き戻してから捨てる（Linux でも munmap で
  // 書いた中身は失われない）
  pthread_mutex_lock(&maps_lock);
  for (struct map **link = &maps; *link != NULL; link = &(*link)->next) {
    if (*link == map) {
      *link = map->next;
      break;
    }
  }
  pthread_mutex_unlock(&maps_lock);
  int failed = write_back(map, (char *)addr, length);
  int saved_errno = errno;
  if (map->fd >= 0)
    close(map->fd);
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
// msync(2)。MAP_SHARED で書けるファイルの写しなら、その範囲をファイルへ書き戻す（MS_ASYNC も、書き戻しを
// 後回しにする理由が無いのでその場で書く）。写しでない番地は ENOMEM（Linux と同じ）
int msync(void *addr, size_t length, int flags) {
  if (((size_t)addr & (PAGESIZE - 1)) != 0 || (flags & ~(MS_ASYNC | MS_SYNC | MS_INVALIDATE)) != 0 ||
      (flags & (MS_ASYNC | MS_SYNC)) == (MS_ASYNC | MS_SYNC)) {
    errno = EINVAL;
    return -1;
  }
  if (length == 0)
    return 0;
  struct map *map = find_map(addr, length);
  if (map == NULL) {
    errno = ENOMEM;
    return -1;
  }
  return write_back(map, (char *)addr, length);
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
