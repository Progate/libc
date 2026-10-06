// ネットワークインターフェースの名前と番号（net/if.h）。
//
// BrowserOS のプロセスから見えるのはカーネルのソケット表で、外への口はカーネルの gateway が持つ（プロセスは外の
// どの口から出るかを選べない）。プロセスが番号で選べる口はループバック（127.0.0.1・::1。カーネルの表の中で
// 繋がる）だけなので、Linux のループバックと同じく `lo` を番号 1 として答える

#define _GNU_SOURCE
#include <errno.h>
#include <net/if.h>
#include <stdlib.h>
#include <string.h>

#define LOOPBACK_NAME "lo"
#define LOOPBACK_INDEX 1

unsigned int if_nametoindex(const char *name) {
  if (name != NULL && strcmp(name, LOOPBACK_NAME) == 0)
    return LOOPBACK_INDEX;
  errno = ENODEV;
  return 0;
}

char *if_indextoname(unsigned int index, char *name) {
  if (index != LOOPBACK_INDEX) {
    errno = ENXIO;
    return NULL;
  }
  strcpy(name, LOOPBACK_NAME);
  return name;
}

struct if_nameindex *if_nameindex(void) {
  // 表と名前を 1 つの割り当てに置く（if_freenameindex が 1 度の free で返せる）
  struct if_nameindex *list = malloc(2 * sizeof *list + sizeof LOOPBACK_NAME);
  if (list == NULL) {
    errno = ENOBUFS;
    return NULL;
  }
  char *names = (char *)(list + 2);
  strcpy(names, LOOPBACK_NAME);
  list[0].if_index = LOOPBACK_INDEX;
  list[0].if_name = names;
  list[1].if_index = 0;
  list[1].if_name = NULL;
  return list;
}

void if_freenameindex(struct if_nameindex *list) {
  free(list);
}
