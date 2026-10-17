// inotify(7)（inotify_init・inotify_init1・inotify_add_watch・inotify_rm_watch）。
//
// カーネルの inotify_init1(2) などを呼ぶだけである（→ browser-os の src/wasi/linux/inotify.ts）。出来事は fd を
// read(2) して struct inotify_event の並びで読み、ppoll(2) で待てる。パスは作業ディレクトリからの相対パスでもよい。

#include <browseros/libc.h>
#include <browseros/linux.h>
#include <sys/inotify.h>

#define SYS_inotify_init1 26
#define SYS_inotify_add_watch 27
#define SYS_inotify_rm_watch 28

int inotify_init1(int flags) { return __browseros_syscall_ret(__bos_syscall1(SYS_inotify_init1, flags)); }

int inotify_init(void) { return inotify_init1(0); }

int inotify_add_watch(int fd, const char *pathname, uint32_t mask) {
  return __browseros_syscall_ret(__bos_syscall3(SYS_inotify_add_watch, fd, __bos_ptr(pathname), mask));
}

int inotify_rm_watch(int fd, int wd) { return __browseros_syscall_ret(__bos_syscall2(SYS_inotify_rm_watch, fd, wd)); }
