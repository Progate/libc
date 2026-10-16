// flock(2)。ロックはカーネルの開いたファイルの表にあり、どのプロセスのロックも同じ表で見る。
// LOCK_NB の無い呼び出しは取れるまで返らない（待つのはカーネル）。→ browser-os の src/fs/flock.ts

#include <browseros/linux.h>
#include <sys/file.h>

int flock(int fd, int operation) { return __browseros_syscall_ret(__bos_syscall2(__BOS_SYS_flock, fd, operation)); }
