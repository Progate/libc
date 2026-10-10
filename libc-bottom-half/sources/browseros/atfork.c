// pthread_atfork(3)。
//
// BrowserOS の fork（→ fork/fork.c。asyncify で組んだプログラムが -lbrowseros-fork で使う）が、写す前に `prepare` を
// 登録の逆順で、写した後に親で `parent`・子で `child` を登録の順で呼ぶ（POSIX の pthread_atfork のとおり）。
// fork を使わないプログラムでは呼ばれる時が来ないが、登録すること自体は失敗しない（libuv・OpenSSL・jemalloc などは
// 初めに登録し、失敗を致命的とみなす）。

#include <browseros/libc.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

struct handlers {
  void (*prepare)(void);
  void (*parent)(void);
  void (*child)(void);
};

static struct handlers *registered;
static size_t count;
static size_t capacity;

int pthread_atfork(void (*prepare)(void), void (*parent)(void), void (*child)(void)) {
  if (count == capacity) {
    size_t grown = capacity == 0 ? 8 : capacity * 2;
    struct handlers *table = realloc(registered, grown * sizeof(*table));
    if (!table)
      return ENOMEM;
    registered = table;
    capacity = grown;
  }
  registered[count++] = (struct handlers){prepare, parent, child};
  return 0;
}

void __browseros_run_atfork(int phase) {
  if (phase == 0) {
    for (size_t i = count; i > 0; i--)
      if (registered[i - 1].prepare)
        registered[i - 1].prepare();
    return;
  }
  for (size_t i = 0; i < count; i++) {
    void (*handler)(void) = phase == 1 ? registered[i].parent : registered[i].child;
    if (handler)
      handler();
  }
}
