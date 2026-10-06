#ifndef __wasilibc___struct_sockaddr_un_h
#define __wasilibc___struct_sockaddr_un_h

#include <__typedef_sa_family_t.h>
#include <features.h>

#ifdef __wasilibc_browseros
/* BrowserOS のカーネルは UNIX ドメインのソケットをパスの名前で持つ（→ sources/browseros/socket.c） */
struct sockaddr_un {
  sa_family_t sun_family;
  char sun_path[108];
};
#else
struct sockaddr_un {
  __attribute__((aligned(__BIGGEST_ALIGNMENT__))) sa_family_t sun_family;
};
#endif

#endif
