#ifndef __wasilibc___struct_dirent_h
#define __wasilibc___struct_dirent_h

#include <__typedef_ino_t.h>
#include <__typedef_off_t.h>

#define _DIRENT_HAVE_D_RECLEN
#define _DIRENT_HAVE_D_OFF
#define _DIRENT_HAVE_D_TYPE

/* BrowserOS: riscv32 の Linux（musl）と同じ並び */
struct dirent {
  ino_t d_ino;
  off_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[256];
};

#endif
