// ネットワークインターフェースの名前と番号（net/if.h）。
//
// BrowserOS のプロセスから見えるのはカーネルのソケット表で、外への口はカーネルの gateway が持つ（プロセスは外の
// どの口から出るかを選べない）。プロセスが番号で選べる口はループバック（127.0.0.1・::1。カーネルの表の中で
// 繋がる）だけなので、Linux のループバックと同じく `lo` を番号 1 として答える

#define _GNU_SOURCE
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
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

/*
 * インターフェースの番地の一覧（ifaddrs.h）。`lo` の 127.0.0.1/8 と ::1/128 の 2 つだけを答える——
 * 上と同じく、外への口はカーネルの gateway が持つので、プロセスが選べる口はループバックしか無い
 */
int getifaddrs(struct ifaddrs **ifap) {
  // 表・名前・番地を 1 つの割り当てに置く（freeifaddrs が 1 度の free で返せる）
  struct {
    struct ifaddrs list[2];
    char name[sizeof LOOPBACK_NAME];
    struct sockaddr_in in_addr;
    struct sockaddr_in in_netmask;
    struct sockaddr_in6 in6_addr;
    struct sockaddr_in6 in6_netmask;
  } *block = malloc(sizeof *block);
  if (block == NULL) {
    errno = ENOBUFS;
    return -1;
  }
  strcpy(block->name, LOOPBACK_NAME);
  memset(&block->in_addr, 0, sizeof block->in_addr);
  block->in_addr.sin_family = AF_INET;
  block->in_addr.sin_addr.s_addr = htonl(0x7f000001);
  memset(&block->in_netmask, 0, sizeof block->in_netmask);
  block->in_netmask.sin_family = AF_INET;
  block->in_netmask.sin_addr.s_addr = htonl(0xff000000);
  memset(&block->in6_addr, 0, sizeof block->in6_addr);
  block->in6_addr.sin6_family = AF_INET6;
  block->in6_addr.sin6_addr.s6_addr[15] = 1;
  memset(&block->in6_netmask, 0, sizeof block->in6_netmask);
  block->in6_netmask.sin6_family = AF_INET6;
  memset(block->in6_netmask.sin6_addr.s6_addr, 0xff, 16);

  const unsigned flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
  block->list[0].ifa_next = &block->list[1];
  block->list[0].ifa_name = block->name;
  block->list[0].ifa_flags = flags;
  block->list[0].ifa_addr = (struct sockaddr *)&block->in_addr;
  block->list[0].ifa_netmask = (struct sockaddr *)&block->in_netmask;
  block->list[0].ifa_broadaddr = NULL;
  block->list[0].ifa_data = NULL;
  block->list[1].ifa_next = NULL;
  block->list[1].ifa_name = block->name;
  block->list[1].ifa_flags = flags;
  block->list[1].ifa_addr = (struct sockaddr *)&block->in6_addr;
  block->list[1].ifa_netmask = (struct sockaddr *)&block->in6_netmask;
  block->list[1].ifa_broadaddr = NULL;
  block->list[1].ifa_data = NULL;
  *ifap = block->list;
  return 0;
}

void freeifaddrs(struct ifaddrs *list) {
  free(list);
}
