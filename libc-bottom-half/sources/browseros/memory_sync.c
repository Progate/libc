// プロセスをまたぐ MAP_SHARED の写しを同期する点（POSIX の Base Definitions 4.12 Memory Synchronization）。
//
// 写しはそのプロセスの malloc の領域にあり、ほかのプロセスの写しとは重なっていない（→ libc-bottom-half/mman/mman.c の
// __browseros_shared_maps_sync）。そこで、メモリを同期すると POSIX が定める関数と、ほかのプロセスと話す口で同期する。
//
//   書いたものを出す（呼ぶ前）  … sem_post・posix_spawn・execve・_Exit・パイプやソケットへの write と send
//   ほかのものを取り込む（返った後）… sem_wait・sem_timedwait・sem_trywait・waitpid の仲間・パイプやソケットからの read と recv
//
// 同期そのものは出すのと取り込むのを両方する（どちらの点でも、自分の書いたものを出してから取り込む）。
//
// mmap の写しは libwasi-emulated-mman にあるので、その中身を弱い参照で呼ぶ。mmap を使わないプログラムには何も
// 入らず、MAP_SHARED のファイルの写しを持たないプロセスでは数を 1 つ見るだけで戻る。

#include <browseros/libc.h>
#include <wasi/api.h>

extern volatile int __browseros_shared_map_count __attribute__((__weak__));
void __browseros_shared_maps_sync(void) __attribute__((__weak__));

void __browseros_memory_sync(void) {
  if (&__browseros_shared_map_count != 0 && __browseros_shared_map_count > 0 && __browseros_shared_maps_sync)
    __browseros_shared_maps_sync();
}

void __browseros_memory_sync_fd(int fd) {
  if (&__browseros_shared_map_count == 0 || __browseros_shared_map_count <= 0)
    return;
  // 普通のファイルとディレクトリの読み書きは、ほかのプロセスと話す口ではない（pip が wheel を読むたびに大きな写しを
  // 読み直さない）。パイプ・ソケット・端末は同期する
  __wasi_fdstat_t stat;
  if (__wasi_fd_fdstat_get(fd, &stat) == 0 &&
      (stat.fs_filetype == __WASI_FILETYPE_REGULAR_FILE || stat.fs_filetype == __WASI_FILETYPE_DIRECTORY))
    return;
  __browseros_memory_sync();
}
