// flock(2)。ロックはカーネルの開いたファイルの表にあり、どのプロセスのロックも同じ表で見る。
// LOCK_NB の無い呼び出しは取れるまで返らない（待つのはカーネル）。→ browser-os の src/fs/flock.ts

#include <browseros/host.h>
#include <errno.h>
#include <sys/file.h>

int flock(int fd, int operation) {
  int32_t error = __browseros_flock(fd, operation);
  if (error != 0) {
    errno = error;
    return -1;
  }
  return 0;
}
