// pthread_atfork(3)。
//
// BrowserOS に fork は無く、子は posix_spawn で起こす（POSIX でも posix_spawn は fork の処理を呼ばなくてよい）。
// 登録した処理が呼ばれる時は来ないが、登録すること自体は失敗しない（libuv・OpenSSL・jemalloc などは
// 初めに登録し、失敗を致命的とみなす）。呼ばれない処理を覚えても意味が無いので、受け付けるだけにする

#include <pthread.h>

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void)) {
  (void)prepare;
  (void)parent;
  (void)child;
  return 0;
}
